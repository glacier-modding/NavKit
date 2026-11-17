#include "../../include/NavKit/util/GtkIntegration.h"

#include <gtk/gtk.h>

namespace GtkIntegration {
    void restrictToX11Backend() {
        gdk_set_allowed_backends("x11");
    }

    void realize(void* gtkWidget) {
        gtk_widget_realize(static_cast<GtkWidget*>(gtkWidget));
    }

    void dispatchReadyLowPriorityWork() {
        // Without this, wx's DispatchTimeout(0) starves GDK's initial draw
        constexpr int maxIterations = 64;
        for (int iteration = 0; iteration < maxIterations && g_main_context_iteration(nullptr, FALSE); ++iteration) {
        }
    }
} // namespace GtkIntegration
