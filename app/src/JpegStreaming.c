#include <gst/gst.h>

#define severe 0.9
#define poor 0.8
#define good 0.6
#define underused 0.5

struct Bandwidth_info {
    double in_use;
    double maximum;
};

struct Elements {
    GstElement *source;
    GstElement *capsfilter;
    GstElement *encoder;
    GstElement *payloader;
    GstElement *sink;
};

void get_bandwidth(GstElement *srtsink, struct Bandwidth_info *out) {
    GstStructure *stats = NULL;

    g_object_get(srtsink, "stats", &stats, NULL);

    if (gst_structure_has_field(stats, "callers")) {
        GValueArray *callers_stats = NULL;
        GValue *v;
        gst_structure_get(stats, "callers", G_TYPE_VALUE_ARRAY, &callers_stats);
        gst_structure_free(stats);
        stats = NULL;

        if (!callers_stats) {
            g_printerr("Error: Failed to retrieve callers stats.\n");
            return;
        }
        if (callers_stats->n_values != 1) {
            g_printerr("Warning: Expected 1 caller but found %u.\n", callers_stats->n_values);
        }
        v = g_value_array_get_nth(callers_stats, 0);
        stats = g_value_get_boxed(v);
        if (!stats) {
            g_printerr("Error: Failed to get stats for the caller.\n");
            return;
        }
    }

    if (stats != NULL) {
        if (gst_structure_has_field(stats, "send-rate-mbps")) {
            gst_structure_get_double(stats, "send-rate-mbps", &out->in_use);
        }
        if (gst_structure_has_field(stats, "bandwidth-mbps")) {
            gst_structure_get_double(stats, "bandwidth-mbps", &out->maximum);
        }
        gst_structure_free(stats);
    } else {
        g_printerr("Failed to retrieve SRT stats from srtsink.\n");
    }
}

void handle_bandwidth(struct Bandwidth_info *info, struct Elements elements) {
    double percent = info->in_use / info->maximum;
    gint quality;

    g_object_get(elements.encoder, "quality", &quality, NULL);

    // Adjust the JPEG encoder quality based on bandwidth usage
    if (percent > severe) {
        quality = (quality * 0.9);  // Decrease quality by 10%
        g_object_set(elements.encoder, "quality", quality, NULL);
        g_print("Severe bandwidth usage. Decreasing quality to %d.\n", quality);
    } else if (percent > poor) {
        quality = (quality * 0.95);  // Decrease quality by 5%
        g_object_set(elements.encoder, "quality", quality, NULL);
        g_print("Poor bandwidth usage. Decreasing quality to %d.\n", quality);
    } else if (percent > good) {
        g_print("Good bandwidth usage, considering increasing quality.\n");
    } else if (percent > underused) {
        quality = (quality * 1.05);  // Increase quality by 5%
        g_object_set(elements.encoder, "quality", quality, NULL);
        g_print("Underused bandwidth. Increasing quality to %d.\n", quality);
    } else {
        quality = (quality * 1.1);  // Increase quality by 10%
        g_object_set(elements.encoder, "quality", quality, NULL);
        g_print("Very underused bandwidth. Increasing quality to %d.\n", quality);
    }
}

// Function to process messages from the GStreamer bus and query bandwidth stats
gboolean run_loop(GstBus *bus, struct Elements *elements) {
    GstMessage *msg;
    gboolean terminate = FALSE;
    struct Bandwidth_info bandwidth_info = {0};

    // Check for messages with a 1-second timeout
    msg = gst_bus_timed_pop_filtered(bus, GST_SECOND, GST_MESSAGE_ERROR | GST_MESSAGE_EOS);

    // If a message is received, handle it
    if (msg != NULL) {
        GError *err = NULL;
        gchar *debug_info = NULL;

        switch (GST_MESSAGE_TYPE(msg)) {
            case GST_MESSAGE_ERROR:
                gst_message_parse_error(msg, &err, &debug_info);
                g_printerr("Error received from element %s: %s\n", 
                           GST_OBJECT_NAME(msg->src), err->message);
                g_printerr("Debugging information: %s\n", debug_info ? debug_info : "none");
                g_clear_error(&err);
                g_free(debug_info);
                terminate = TRUE;
                break;
            case GST_MESSAGE_EOS:
                g_print("End-Of-Stream reached.\n");
                terminate = TRUE;
                break;
            default:
                g_printerr("Unexpected message received.\n");
                break;
        }
        gst_message_unref(msg);
    }

    // Call the get_bandwidth function to fetch bandwidth stats
    get_bandwidth(elements->sink, &bandwidth_info);

    // Call the handle_bandwidth function to adjust quality based on bandwidth
    handle_bandwidth(&bandwidth_info, *elements);

    return terminate;
}

int main(int argc, char *argv[]) {
    GstElement *pipeline, *source, *capsfilter, *encoder, *payloader, *sink;
    GstCaps *caps;
    GstBus *bus;
    GstStateChangeReturn ret;
    gboolean terminate = FALSE;

    // Initialize GStreamer
    gst_init(&argc, &argv);

    // Create pipeline elements
    source = gst_element_factory_make("v4l2src", "source");
    capsfilter = gst_element_factory_make("capsfilter", "capsfilter");
    encoder = gst_element_factory_make("nvjpegenc", "encoder");
    payloader = gst_element_factory_make("rtpjpegpay", "payloader");
    sink = gst_element_factory_make("srtsink", "sink");

    // Create the empty pipeline
    pipeline = gst_pipeline_new("video-pipeline");

    if (!pipeline || !source || !capsfilter || !encoder || !payloader || !sink) {
        g_printerr("Not all elements could be created.\n");
        return -1;
    }

    // Set the properties for the source element
    g_object_set(source, "device", "/dev/video0", NULL); // Adjust the device path if necessary

    // Configure srtsink in listener mode using URI parameters
    g_object_set(sink, "uri", "srt://0.0.0.0:8888?mode=listener", NULL);

    // Define the caps filter (raw video format: e.g., YUY2, 640x480, 30fps)
    caps = gst_caps_from_string("video/x-raw,format=YUY2,width=640,height=480,framerate=30/1");
    g_object_set(capsfilter, "caps", caps, NULL);
    gst_caps_unref(caps);

    // Build the pipeline: source -> capsfilter -> encoder -> payloader -> sink
    gst_bin_add_many(GST_BIN(pipeline), source, capsfilter, encoder, payloader, sink, NULL);
    if (!gst_element_link_many(source, capsfilter, encoder, payloader, sink, NULL)) {
        g_printerr("Elements could not be linked.\n");
        gst_object_unref(pipeline);
        return -1;
    }

    // Start playing the pipeline
    ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        g_printerr("Unable to set the pipeline to the playing state.\n");
        gst_object_unref(pipeline);
        return -1;
    }

    // Get the bus to listen for messages
    bus = gst_element_get_bus(pipeline);
    struct Elements elements = {source, capsfilter, encoder, payloader, sink};

    // Main loop: use the function run_loop to process bus messages and adjust quality
    while (!terminate) {
        terminate = run_loop(bus, &elements);
    }

    // Clean up
    gst_object_unref(bus);
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);

    return 0;
}