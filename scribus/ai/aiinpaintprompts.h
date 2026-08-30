/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#ifndef AIINPAINTPROMPTS_H
#define AIINPAINTPROMPTS_H

/*!
 \brief The instructions sent to a vision model that edits a whole picture.

 None of the hosted image models takes a separate mask channel the way LaMa
 does. The area to remove therefore has to be said in the only language they
 all read: it is painted onto the picture as a bright red overlay, and the
 prompt tells the model that red means "remove this and rebuild what was
 behind it".

 That makes the prompt part of the wire format rather than a piece of English,
 and it is shared because every service that composites a red mask has to say
 exactly the same thing. Two copies that drift apart would mean the same
 removal came out differently depending on which provider the user picked, for
 no reason either of them could see.

 Not translated, deliberately. It is addressed to a model, not to a person,
 and these models follow English instructions best; a Malayalam or German
 build must not quietly send a different instruction and get a different
 picture back.
 */
namespace AIInpaintPrompts
{
	/*! \brief Remove what the red overlay covers, from a single composited
	    reference image.

	    Used by the OpenRouter and Gemini services. It has to do the job a mask
	    channel does elsewhere: say what the red is, say that everything else
	    must come back untouched, and shut the door on the things these models
	    like to add unasked. */
	constexpr const char* REMOVE_OBJECT_COMPOSITE =
		"You are an expert photo editor. The reference image has an area "
		"highlighted with a bright red mask overlay. Return the exact same "
		"image with the red-highlighted area completely removed and the "
		"background naturally reconstructed. Match the surrounding lighting, "
		"texture, perspective, colors, and content. The result must look "
		"photorealistic, as if the object was never there. Do not modify any "
		"other parts of the image. Do not add watermarks, borders, or text. "
		"Return only the edited image.";
}

#endif
