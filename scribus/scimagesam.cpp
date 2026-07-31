/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "scimagesam.h"

#include <QDir>
#include <QFileInfo>
#include <QMutex>
#include <QObject>

#ifdef HAVE_SAM
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <vector>

#include <QThread>

#include <onnxruntime_cxx_api.h>
#endif

QString SamSegmenter::modelDir()
{
	return QDir::homePath() + QStringLiteral("/.config/scribus/sam/");
}

// ── Impl ─────────────────────────────────────────────────────────────────────

struct SamSegmenter::Impl
{
	// The Smart Select tool encodes on a worker thread while the GUI thread may
	// query state or (on quick tool switches) start another encode; recursive
	// because setImage() calls ensureLoaded() internally.
	QRecursiveMutex mutex;
	QString status;
#ifdef HAVE_SAM
	Ort::Env env { ORT_LOGGING_LEVEL_WARNING, "scribus-sam" };
	std::unique_ptr<Ort::Session> encoder;
	std::unique_ptr<Ort::Session> decoder;
	std::string encInputName;
	std::string encOutputName;
	bool loaded { false };

	std::vector<float> embedding;   // cached image_embeddings [1,256,64,64]
	int origW { 0 };
	int origH { 0 };
	float scale { 1.0f };
	bool haveImage { false };
#endif
	bool antialias { false };
};

#ifdef HAVE_SAM
namespace
{
	// Run the SAM/MobileSAM mask decoder for a prompt (coords in the resized
	// 1024-longest-side space; labels: 1=fg, 0=bg, 2/3=box corners, -1=pad).
	QImage runSamDecoder(Ort::Session& decoder, std::vector<float>& embedding,
	                     int origW, int origH, std::vector<float>& coords,
	                     std::vector<float>& labels, QString& status, bool antialias)
	{
		try
		{
			const int64_t N = static_cast<int64_t>(labels.size());
			std::array<int64_t, 4> embShape { 1, 256, 64, 64 };
			std::array<int64_t, 3> coordShape { 1, N, 2 };
			std::array<int64_t, 2> labelShape { 1, N };
			std::array<int64_t, 4> maskShape { 1, 1, 256, 256 };
			std::array<int64_t, 1> hasMaskShape { 1 };
			std::array<int64_t, 1> origShape { 2 };

			std::vector<float> maskInput(256 * 256, 0.0f);
			std::vector<float> hasMask { 0.0f };
			std::vector<float> origSize { static_cast<float>(origH), static_cast<float>(origW) };

			Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
			std::vector<Ort::Value> inputs;
			inputs.reserve(6);
			inputs.push_back(Ort::Value::CreateTensor<float>(mem, embedding.data(), embedding.size(), embShape.data(), embShape.size()));
			inputs.push_back(Ort::Value::CreateTensor<float>(mem, coords.data(), coords.size(), coordShape.data(), coordShape.size()));
			inputs.push_back(Ort::Value::CreateTensor<float>(mem, labels.data(), labels.size(), labelShape.data(), labelShape.size()));
			inputs.push_back(Ort::Value::CreateTensor<float>(mem, maskInput.data(), maskInput.size(), maskShape.data(), maskShape.size()));
			inputs.push_back(Ort::Value::CreateTensor<float>(mem, hasMask.data(), hasMask.size(), hasMaskShape.data(), hasMaskShape.size()));
			inputs.push_back(Ort::Value::CreateTensor<float>(mem, origSize.data(), origSize.size(), origShape.data(), origShape.size()));

			const char* inNames[] = { "image_embeddings", "point_coords", "point_labels", "mask_input", "has_mask_input", "orig_im_size" };
			const char* outNames[] = { "masks", "iou_predictions" };
			auto outs = decoder.Run(Ort::RunOptions { nullptr }, inNames, inputs.data(), inputs.size(), outNames, 2);

			float* masks = outs[0].GetTensorMutableData<float>();
			auto mShape = outs[0].GetTensorTypeAndShapeInfo().GetShape();   // [1, M, H, W]
			if (mShape.size() != 4)
			{
				status = QObject::tr("SAM: unexpected decoder output shape");
				return QImage();
			}
			int M = static_cast<int>(mShape[1]);
			int H = static_cast<int>(mShape[2]);
			int W = static_cast<int>(mShape[3]);
			int best = 0;
			if (M > 1)
			{
				float* iou = outs[1].GetTensorMutableData<float>();
				for (int i = 1; i < M; ++i)
					if (iou[i] > iou[best])
						best = i;
			}

			QImage out(W, H, QImage::Format_Alpha8);
			const float* mp = masks + static_cast<size_t>(best) * H * W;
			for (int y = 0; y < H; ++y)
			{
				uchar* dst = out.scanLine(y);
				for (int x = 0; x < W; ++x)
				{
					const float l = mp[y * W + x];   // decoder logit; boundary at 0
					if (antialias)
					{
						// Ramp coverage across the zero-crossing (the mask is
						// bilinearly upsampled to full res, so logits vary smoothly
						// over the edge). ±1 logit → ~1px soft, anti-aliased edge.
						const float a = l * 0.5f + 0.5f;
						dst[x] = static_cast<uchar>(qBound(0, static_cast<int>(std::lround(a * 255.0f)), 255));
					}
					else
					{
						dst[x] = (l > 0.0f) ? 255 : 0;   // hard threshold (crisp)
					}
				}
			}
			status = QObject::tr("SAM ready");
			return out;
		}
		catch (const std::exception& e)
		{
			status = QObject::tr("SAM: decoder failed (%1)").arg(QString::fromUtf8(e.what()));
			return QImage();
		}
	}
}
#endif // HAVE_SAM

// ── SamSegmenter ─────────────────────────────────────────────────────────────

SamSegmenter& SamSegmenter::instance()
{
	static SamSegmenter s;
	return s;
}

SamSegmenter::SamSegmenter()
	: d(new Impl)
{
#ifdef HAVE_SAM
	d->status = QObject::tr("SAM: models not loaded");
#else
	d->status = QObject::tr("SAM: not built (ONNX Runtime unavailable)");
#endif
}

SamSegmenter::~SamSegmenter()
{
	delete d;
}

QString SamSegmenter::statusMessage() const
{
	QMutexLocker locker(&d->mutex);
	return d->status;
}

void SamSegmenter::setAntialias(bool on)
{
	QMutexLocker locker(&d->mutex);
	d->antialias = on;
}

bool SamSegmenter::antialias() const
{
	QMutexLocker locker(&d->mutex);
	return d->antialias;
}

bool SamSegmenter::isAvailable() const
{
#ifdef HAVE_SAM
	return d->loaded;
#else
	return false;
#endif
}

bool SamSegmenter::ensureLoaded()
{
	QMutexLocker locker(&d->mutex);
#ifdef HAVE_SAM
	if (d->loaded)
		return true;
	const QString enc = modelDir() + QStringLiteral("encoder.onnx");
	const QString dec = modelDir() + QStringLiteral("decoder.onnx");
	if (!QFileInfo::exists(enc) || !QFileInfo::exists(dec))
	{
		d->status = QObject::tr("SAM: place encoder.onnx and decoder.onnx in %1").arg(modelDir());
		return false;
	}
	try
	{
		Ort::SessionOptions opts;
		opts.SetIntraOpNumThreads(qMax(1, QThread::idealThreadCount()));
		opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
		d->encoder = std::make_unique<Ort::Session>(d->env, enc.toStdString().c_str(), opts);
		d->decoder = std::make_unique<Ort::Session>(d->env, dec.toStdString().c_str(), opts);
		Ort::AllocatorWithDefaultOptions alloc;
		d->encInputName  = d->encoder->GetInputNameAllocated(0, alloc).get();
		d->encOutputName = d->encoder->GetOutputNameAllocated(0, alloc).get();
		d->loaded = true;
		d->status = QObject::tr("SAM ready");
		return true;
	}
	catch (const std::exception& e)
	{
		d->status = QObject::tr("SAM: failed to load models (%1)").arg(QString::fromUtf8(e.what()));
		d->loaded = false;
		return false;
	}
#else
	return false;
#endif
}

bool SamSegmenter::setImage(const QImage& image)
{
	QMutexLocker locker(&d->mutex);
#ifdef HAVE_SAM
	if (!ensureLoaded() || image.isNull())
		return false;
	try
	{
		// This MobileSAM/AnyLabeling encoder takes the image resized so the longest
		// side is 1024, as a raw HWC float tensor [H, W, 3] (RGB, 0-255); it does the
		// SAM normalization + pad-to-1024 internally.
		QImage img = image.convertToFormat(QImage::Format_ARGB32);
		d->origW = img.width();
		d->origH = img.height();
		const int longSide = std::max(d->origW, d->origH);
		d->scale = 1024.0f / longSide;
		const int newW = std::max(1, static_cast<int>(std::lround(d->origW * d->scale)));
		const int newH = std::max(1, static_cast<int>(std::lround(d->origH * d->scale)));
		QImage resized = img.scaled(newW, newH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

		std::vector<float> input(static_cast<size_t>(newH) * newW * 3);
		for (int y = 0; y < newH; ++y)
		{
			const QRgb* s = reinterpret_cast<const QRgb*>(resized.constScanLine(y));
			for (int x = 0; x < newW; ++x)
			{
				QRgb p = s[x];
				size_t base = (static_cast<size_t>(y) * newW + x) * 3;
				input[base + 0] = qRed(p);
				input[base + 1] = qGreen(p);
				input[base + 2] = qBlue(p);
			}
		}

		std::array<int64_t, 3> shape { newH, newW, 3 };
		Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
		Ort::Value inTensor = Ort::Value::CreateTensor<float>(mem, input.data(), input.size(), shape.data(), shape.size());
		const char* inNames[] = { d->encInputName.c_str() };
		const char* outNames[] = { d->encOutputName.c_str() };
		auto outs = d->encoder->Run(Ort::RunOptions { nullptr }, inNames, &inTensor, 1, outNames, 1);

		float* emb = outs[0].GetTensorMutableData<float>();
		auto embShape = outs[0].GetTensorTypeAndShapeInfo().GetShape();
		size_t n = 1;
		for (auto v : embShape)
			n *= static_cast<size_t>(v);
		d->embedding.assign(emb, emb + n);
		d->haveImage = true;
		d->status = QObject::tr("SAM ready");
		return true;
	}
	catch (const std::exception& e)
	{
		d->status = QObject::tr("SAM: encoder failed (%1)").arg(QString::fromUtf8(e.what()));
		d->haveImage = false;
		return false;
	}
#else
	Q_UNUSED(image)
	return false;
#endif
}

QImage SamSegmenter::segmentAtPoint(const QPoint& pt, bool foreground)
{
	QMutexLocker locker(&d->mutex);
	return segmentAtPoints({ pt }, { foreground ? 1 : 0 });
}

QImage SamSegmenter::segmentAtPoints(const QVector<QPoint>& points, const QVector<int>& labels)
{
	QMutexLocker locker(&d->mutex);
#ifdef HAVE_SAM
	if (!d->haveImage || points.isEmpty())
		return QImage();
	std::vector<float> coords;
	std::vector<float> labs;
	coords.reserve((points.size() + 1) * 2);
	labs.reserve(points.size() + 1);
	for (int i = 0; i < points.size(); ++i)
	{
		coords.push_back(points[i].x() * d->scale);
		coords.push_back(points[i].y() * d->scale);
		labs.push_back(i < labels.size() ? static_cast<float>(labels[i]) : 1.0f);
	}
	coords.push_back(0.0f);   // padding point required when there is no box
	coords.push_back(0.0f);
	labs.push_back(-1.0f);
	return runSamDecoder(*d->decoder, d->embedding, d->origW, d->origH, coords, labs, d->status, d->antialias);
#else
	Q_UNUSED(points)
	Q_UNUSED(labels)
	return QImage();
#endif
}

QImage SamSegmenter::segmentInBox(const QRect& box)
{
	QMutexLocker locker(&d->mutex);
#ifdef HAVE_SAM
	if (!d->haveImage)
		return QImage();
	std::vector<float> coords {
		box.left()  * d->scale, box.top()    * d->scale,
		box.right() * d->scale, box.bottom() * d->scale
	};
	std::vector<float> labels { 2.0f, 3.0f };   // box top-left, bottom-right
	return runSamDecoder(*d->decoder, d->embedding, d->origW, d->origH, coords, labels, d->status, d->antialias);
#else
	Q_UNUSED(box)
	return QImage();
#endif
}
