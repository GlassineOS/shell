"""glassine-settings: the device's touch and e-ink settings app.

Settings draws itself at the factor of the display it is on (spec 012), its text at
absolute sizes: GDK_DPI_SCALE, the text scale the session gives whole-scale-only GTK apps
(FR-031), is not for it and is taken out of its environment before GTK starts.
GDK_DPI_SCALE below keeps it for the programs Settings starts (predict.open_folder)."""
import os

GDK_DPI_SCALE = os.environ.pop("GDK_DPI_SCALE", None)


def child_env():
    """the environment for a program Settings starts: its own text scale back"""
    env = dict(os.environ)
    if GDK_DPI_SCALE is not None:
        env["GDK_DPI_SCALE"] = GDK_DPI_SCALE
    return env
