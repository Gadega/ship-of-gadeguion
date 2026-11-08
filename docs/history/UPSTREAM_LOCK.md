# Inspected upstream revisions

The initial audit on 2026-09-20 used these exact revisions:

| Project | Revision | Role |
| --- | --- | --- |
| HarbourMasters/Shipwright | `04ccd608b3884713bf6bf6284f5d272c256fc507` | Game port and integration |
| kenix3/libultraship | `62e973aeb4a53ad4d22bb91e2d9373ecdfcd246c` | Platform, resources, Fast3D renderer |
| HarbourMasters/Torch | `2ab12fe9660aec04e02ee89fe81baed304a1a1d6` | Asset extraction/build tooling |
| ps3dev/PSL1GHT | `f649a08fd536a9e27c08c7db2d93a2d7ee4c3bbe` | Open-source PS3 SDK |
| ps3dev/ps3dev | `bc09c6292f1be95be874cfda9226ea9ba9ceaef3` | Complete PS3 development environment |

Use these revisions to reproduce the audit. Do not vendor Shipwright, a ROM, or
generated `.o2r` asset archives into this repository.
