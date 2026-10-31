#!/usr/bin/env python3
"""A Secret Service that owns org.freedesktop.secrets and never answers a call
(tests/suneer/news_login.sh, case "noanswer"): the keyring check must give
up after ~3 s instead of waiting."""
from gi.repository import Gio, GLib

XML = """<node><interface name="org.freedesktop.Secret.Service">
<method name="ReadAlias"><arg type="s" direction="in"/><arg type="o" direction="out"/></method>
</interface><interface name="org.freedesktop.DBus.Properties">
<method name="Get"><arg type="s" direction="in"/><arg type="s" direction="in"/><arg type="v" direction="out"/></method>
</interface></node>"""
held = []


def on_call(conn, sender, path, iface, method, params, invocation):
    held.append(invocation)          # never returned: the caller waits


def on_bus(conn, name):
    for iface in Gio.DBusNodeInfo.new_for_xml(XML).interfaces:
        conn.register_object("/org/freedesktop/secrets", iface, on_call, None, None)


Gio.bus_own_name(Gio.BusType.SESSION, "org.freedesktop.secrets", Gio.BusNameOwnerFlags.NONE, on_bus, None, None)
GLib.MainLoop().run()
