"""glassine-setup: the first-run setup.

The setup draws itself at the factor of the display it is on (shell spec 012), its text at
absolute sizes: GDK_DPI_SCALE, the text scale the session gives whole-scale-only GTK apps,
is not for it and is taken out of its environment before GTK starts."""
import os

os.environ.pop("GDK_DPI_SCALE", None)
