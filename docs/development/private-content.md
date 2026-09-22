<p align="right"><a href="private-content.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Public firmware core and private content

This branch contains the application core, progress protocol, resource tools,
partition layout and host tests. The current itinerary, background authoring
assets, generated card pack, matching font subsets and itinerary documentation
are delivered as a separately versioned private overlay. They are intentionally
not published with this core change.

Before building, apply the matching private overlay to an isolated checkout of
the exact firmware commit recorded by its manifest. Verify every overlay file
hash, and verify that its activity catalogue agrees with the voice service.
Then run `./tools/validate.sh` and the production UI renderer.

The legacy content inherited from this branch's base commit is not a valid
version-3 delivery pack. A public checkout alone is therefore not a complete
flashable release. Do not bypass resource validation or substitute an old
embedded fallback. The delivered application must embed the same complete
validated pack that is written to its content partition.

Credentials and device-specific runtime configuration stay outside Git.
Flashing, data preservation and verification follow the normal firmware-layout
and device-test procedures.
