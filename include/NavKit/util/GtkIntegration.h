#pragma once

// Kept out of other headers so GTK and GLib's macros don't leak into NavKit code
namespace GtkIntegration {
    void restrictToX11Backend();

    void realize(void* gtkWidget);

    void dispatchReadyLowPriorityWork();
} // namespace GtkIntegration
