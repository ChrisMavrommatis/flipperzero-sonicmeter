#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_input.h>
#include <gui/modules/widget.h>
#include <gui/modules/variable_item_list.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>
#include "sonicmeter_icons.h"

#define TAG "SonicMeter"

// Change this to BACKLIGHT_AUTO if you don't want the backlight to be continuously on.
#define BACKLIGHT_ON 1

// Our application menu has 3 items.  You can add more items if you want.
typedef enum {
    SonicMeterSubmenuIndexConfigure,
    SonicMeterSubmenuIndexMeasure,
    SonicMeterSubmenuIndexAbout,
} SonicMeterSubmenuIndex;

// Each view is a screen we show the user.
typedef enum {
    SonicMeterViewSubmenu, // The menu when the app starts
    SonicMeterViewConfigure, // The configuration screen
    SonicMeterViewMeasure, // The main screen
    SonicMeterViewAbout, // The about screen with directions, link to social channel, etc.
} SonicMeterView;

typedef enum {
    SonicMeterEventIdRedrawScreen = 0, // Custom event to redraw the screen
    SonicMeterEventIdOkPressed = 42, // Custom event to process OK button getting pressed down
} SonicMeterEventId;

typedef struct {
    ViewDispatcher* view_dispatcher; // Switches between our views
    NotificationApp* notifications; // Used for controlling the backlight
    Submenu* submenu; // The application menu
    VariableItemList* variable_item_list_config; // The configuration screen
    View* view_measure; // The main screen
    Widget* widget_about; // The about screen

    FuriTimer* timer; // Timer for redrawing the screen
} SonicMeterApp;

typedef struct {
    uint32_t setting_triggerpin_index; // The trigger pin setting index
    uint32_t setting_echopin_index; // The echo pin setting index
} SonicMeterMeasureModel;

/**
 * @brief      Callback for exiting the application.
 * @details    This function is called when user press back button.  We return VIEW_NONE to
 *            indicate that we want to exit the application.
 * @param      _context  The context - unused
 * @return     next view id
*/
static uint32_t sonicmeter_navigation_exit_callback(void* _context) {
    UNUSED(_context);
    return VIEW_NONE;
}

/**
 * @brief      Callback for returning to submenu.
 * @details    This function is called when user press back button.  We return VIEW_NONE to
 *            indicate that we want to navigate to the submenu.
 * @param      _context  The context - unused
 * @return     next view id
*/
static uint32_t sonicmeter_navigation_submenu_callback(void* _context) {
    UNUSED(_context);
    return SonicMeterViewSubmenu;
}

/**
 * @brief      Handle submenu item selection.
 * @details    This function is called when user selects an item from the submenu.
 * @param      context  The context - SonicMeterApp object.
 * @param      index     The SonicMeterSubmenuIndex item that was clicked.
*/
static void sonicmeter_submenu_callback(void* context, uint32_t index) {
    SonicMeterApp* app = (SonicMeterApp*)context;
    switch(index) {
    case SonicMeterSubmenuIndexConfigure:
        view_dispatcher_switch_to_view(app->view_dispatcher, SonicMeterViewConfigure);
        break;
    case SonicMeterSubmenuIndexMeasure:
        view_dispatcher_switch_to_view(app->view_dispatcher, SonicMeterViewMeasure);
        break;
    case SonicMeterSubmenuIndexAbout:
        view_dispatcher_switch_to_view(app->view_dispatcher, SonicMeterViewAbout);
        break;
    default:
        break;
    }
}

/**
 * Our 1st sample setting is a list of values.  When the user clicks OK on the configuration
*/
static const char* setting_triggerpin_config_label = "Trigger Pin";
static uint8_t setting_triggerpin_values[] = {1, 2, 3};
static char* setting_triggerpin_names[] = {"A4", "A6", "A7"};
static void sonicmeter_setting_triggerpin_change(VariableItem* item) {
    SonicMeterApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, setting_triggerpin_names[index]);
    SonicMeterMeasureModel* model = view_get_model(app->view_measure);
    model->setting_triggerpin_index = index;
}

/**
 * Our 2nd sample setting is a list of values.  When the user clicks OK on the configuration
*/
static const char* setting_echopin_config_label = "Echo Pin";
static uint8_t setting_echopin_values[] = {1, 2};
static char* setting_echopin_names[] = {"B2", "B3"};
static void sonicmeter_setting_echopin_change(VariableItem* item) {
    SonicMeterApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, setting_echopin_names[index]);
    SonicMeterMeasureModel* model = view_get_model(app->view_measure);
    model->setting_echopin_index = index;
}

/**
 * @brief      Callback for drawing the measure screen.
 * @details    This function is called when the screen needs to be redrawn, like when the model gets updated.
 * @param      canvas  The canvas to draw on.
 * @param      model   The model - MyModel object.
*/
static void sonicmeter_view_measure_draw_callback(Canvas* canvas, void* model) {
    SonicMeterMeasureModel* m = (SonicMeterMeasureModel*)model;

    canvas_draw_str(canvas, 35, 8, "Sonic Meter");
    FuriString* xstr = furi_string_alloc();

    furi_string_printf(xstr, "Distance: %d cm", 0);
    canvas_draw_str(canvas, 30, 34, furi_string_get_cstr(xstr));

    furi_string_printf(
        xstr, "Trigger pin: %s", setting_triggerpin_names[m->setting_triggerpin_index]);
    canvas_draw_str(canvas, 0, 62, furi_string_get_cstr(xstr));

    furi_string_printf(xstr, "Echo pin: %s", setting_echopin_names[m->setting_echopin_index]);
    canvas_draw_str(canvas, 75, 62, furi_string_get_cstr(xstr));

    furi_string_free(xstr);
}

/**
 * @brief      Callback for timer elapsed.
 * @details    This function is called when the timer is elapsed.  We use this to queue a redraw event.
 * @param      context  The context - SonicMeterApp object.
*/
static void sonicmeter_view_measure_timer_callback(void* context) {
    SonicMeterApp* app = (SonicMeterApp*)context;

    // measure distance

    view_dispatcher_send_custom_event(app->view_dispatcher, SonicMeterEventIdRedrawScreen);
}

/**
 * @brief      Callback when the user starts the measure screen.
 * @details    This function is called when the user enters the measure screen.  We start a timer to
 *           redraw the screen periodically (so the random number is refreshed).
 * @param      context  The context - SonicMeterApp object.
*/
static void sonicmeter_view_measure_enter_callback(void* context) {
    uint32_t period = furi_ms_to_ticks(200);
    SonicMeterApp* app = (SonicMeterApp*)context;
    furi_assert(app->timer == NULL);
    app->timer =
        furi_timer_alloc(sonicmeter_view_measure_timer_callback, FuriTimerTypePeriodic, context);
    furi_timer_start(app->timer, period);
}

/**
 * @brief      Callback when the user exits the measure screen.
 * @details    This function is called when the user exits the measure screen.  We stop the timer.
 * @param      context  The context - SonicMeterApp object.
*/
static void sonicmeter_view_measure_exit_callback(void* context) {
    SonicMeterApp* app = (SonicMeterApp*)context;
    furi_timer_stop(app->timer);
    furi_timer_free(app->timer);
    app->timer = NULL;
}

/**
 * @brief      Callback for custom events.
 * @details    This function is called when a custom event is sent to the view dispatcher.
 * @param      event    The event id - SonicMeterEventId value.
 * @param      context  The context - SonicMeterApp object.
*/
static bool sonicmeter_view_measure_custom_event_callback(uint32_t event, void* context) {
    SonicMeterApp* app = (SonicMeterApp*)context;
    switch(event) {
    case SonicMeterEventIdRedrawScreen: {
        bool redraw = true;
        with_view_model(
            app->view_measure, SonicMeterMeasureModel * _model, { UNUSED(_model); }, redraw);
        return true;
    }
    case SonicMeterEventIdOkPressed: {
        // measure
        return true;
    }
    default:
        return false;
    }
}

/**
 * @brief      Callback for measure screen input.
 * @details    This function is called when the user presses a button while on the measure screen.
 * @param      event    The event - InputEvent object.
 * @param      context  The context - SonicMeterApp object.
 * @return     true if the event was handled, false otherwise.
*/
static bool sonicmeter_view_measure_input_callback(InputEvent* event, void* context) {
    SonicMeterApp* app = (SonicMeterApp*)context;
    if(event->type == InputTypePress) {
        if(event->key == InputKeyOk) {
            view_dispatcher_send_custom_event(app->view_dispatcher, SonicMeterEventIdOkPressed);
            return true;
        }
    }

    return false;
}

/**
 * @brief      Allocate the sonicmeter application.
 * @details    This function allocates the sonicmeter application resources.
 * @return     SonicMeterApp object.
*/
static SonicMeterApp* sonicmeter_app_alloc() {
    SonicMeterApp* app = (SonicMeterApp*)malloc(sizeof(SonicMeterApp));

    Gui* gui = furi_record_open(RECORD_GUI);

    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_attach_to_gui(app->view_dispatcher, gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);

    app->submenu = submenu_alloc();

    submenu_add_item(
        app->submenu, "Config", SonicMeterSubmenuIndexConfigure, sonicmeter_submenu_callback, app);
    submenu_add_item(
        app->submenu, "Measure", SonicMeterSubmenuIndexMeasure, sonicmeter_submenu_callback, app);
    submenu_add_item(
        app->submenu, "About", SonicMeterSubmenuIndexAbout, sonicmeter_submenu_callback, app);

    view_set_previous_callback(
        submenu_get_view(app->submenu), sonicmeter_navigation_exit_callback);
    view_dispatcher_add_view(
        app->view_dispatcher, SonicMeterViewSubmenu, submenu_get_view(app->submenu));
    view_dispatcher_switch_to_view(app->view_dispatcher, SonicMeterViewSubmenu);

    app->variable_item_list_config = variable_item_list_alloc();
    variable_item_list_reset(app->variable_item_list_config);

    // Setup Trigger Pin
    VariableItem* triggerpin_item = variable_item_list_add(
        app->variable_item_list_config,
        setting_triggerpin_config_label,
        COUNT_OF(setting_triggerpin_values),
        sonicmeter_setting_triggerpin_change,
        app);

    uint8_t setting_triggerpin_index = 0;
    variable_item_set_current_value_index(triggerpin_item, setting_triggerpin_index);
    variable_item_set_current_value_text(
        triggerpin_item, setting_triggerpin_names[setting_triggerpin_index]);

    // Setup Trigger Pin
    VariableItem* echopin_item = variable_item_list_add(
        app->variable_item_list_config,
        setting_echopin_config_label,
        COUNT_OF(setting_echopin_values),
        sonicmeter_setting_echopin_change,
        app);

    uint8_t setting_echopin_index = 0;
    variable_item_set_current_value_index(echopin_item, setting_echopin_index);
    variable_item_set_current_value_text(
        echopin_item, setting_echopin_names[setting_echopin_index]);

    view_set_previous_callback(
        variable_item_list_get_view(app->variable_item_list_config),
        sonicmeter_navigation_submenu_callback);

    view_dispatcher_add_view(
        app->view_dispatcher,
        SonicMeterViewConfigure,
        variable_item_list_get_view(app->variable_item_list_config));

    app->view_measure = view_alloc();
    view_set_draw_callback(app->view_measure, sonicmeter_view_measure_draw_callback);
    view_set_input_callback(app->view_measure, sonicmeter_view_measure_input_callback);
    view_set_previous_callback(app->view_measure, sonicmeter_navigation_submenu_callback);
    view_set_enter_callback(app->view_measure, sonicmeter_view_measure_enter_callback);
    view_set_exit_callback(app->view_measure, sonicmeter_view_measure_exit_callback);
    view_set_context(app->view_measure, app);
    view_set_custom_callback(app->view_measure, sonicmeter_view_measure_custom_event_callback);
    view_allocate_model(app->view_measure, ViewModelTypeLockFree, sizeof(SonicMeterMeasureModel));
    SonicMeterMeasureModel* model = view_get_model(app->view_measure);

    model->setting_triggerpin_index = setting_triggerpin_index;
    model->setting_echopin_index = setting_echopin_index;

    view_dispatcher_add_view(app->view_dispatcher, SonicMeterViewMeasure, app->view_measure);

    app->widget_about = widget_alloc();
    widget_add_text_scroll_element(
        app->widget_about,
        0,
        0,
        128,
        64,
        "A simple app that measures distance using the HC-SR04 module.\n\nauthor: Chris Mavrommatis");

    view_set_previous_callback(
        widget_get_view(app->widget_about), sonicmeter_navigation_submenu_callback);
    view_dispatcher_add_view(
        app->view_dispatcher, SonicMeterViewAbout, widget_get_view(app->widget_about));

    app->notifications = furi_record_open(RECORD_NOTIFICATION);

#ifdef BACKLIGHT_ON
    notification_message(app->notifications, &sequence_display_backlight_enforce_on);
#endif

    return app;
}

/**
 * @brief      Free the sonicmeter application.
 * @details    This function frees the sonicmeter application resources.
 * @param      app  The sonicmeter application object.
*/
static void sonicmeter_app_free(SonicMeterApp* app) {
#ifdef BACKLIGHT_ON
    notification_message(app->notifications, &sequence_display_backlight_enforce_auto);
#endif
    furi_record_close(RECORD_NOTIFICATION);

    view_dispatcher_remove_view(app->view_dispatcher, SonicMeterViewAbout);
    widget_free(app->widget_about);
    view_dispatcher_remove_view(app->view_dispatcher, SonicMeterViewMeasure);
    view_free(app->view_measure);
    view_dispatcher_remove_view(app->view_dispatcher, SonicMeterViewConfigure);
    variable_item_list_free(app->variable_item_list_config);
    view_dispatcher_remove_view(app->view_dispatcher, SonicMeterViewSubmenu);
    submenu_free(app->submenu);
    view_dispatcher_free(app->view_dispatcher);
    furi_record_close(RECORD_GUI);

    free(app);
}

/**
 * @brief      Main function for sonicmeter application.
 * @details    This function is the entry point for the sonicmeter application.  It should be defined in
 *           application.fam as the entry_point setting.
 * @param      _p  Input parameter - unused
 * @return     0 - Success
*/
int32_t main_sonicmeter_app(void* _p) {
    UNUSED(_p);

    SonicMeterApp* app = sonicmeter_app_alloc();
    view_dispatcher_run(app->view_dispatcher);

    sonicmeter_app_free(app);
    return 0;
}
