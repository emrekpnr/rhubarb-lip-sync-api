#include "rhubarb/rhubarb_c_api.h"

#include <string>
#include <sstream>
#include <cstring>
#include <filesystem>
#include <exception>
#include <cstdarg>

#include <format.h>

#include "lib/rhubarbLib.h"               // animateWaveFile
#include "animation/targetShapeSet.h"
#include "exporters/JsonExporter.h"
#include "time/ContinuousTimeline.h"
#include "tools/parallel.h"
#include "tools/platformTools.h"
#include "tools/exceptions.h"             // getMessage / nested exceptions helpers in repo

#include "recognition/Recognizer.h"
#include "recognition/PhoneticRecognizer.h"
#include "recognition/PocketSphinxRecognizer.h"
#include "RecognizerType.h"               // enum RecognizerType

#if defined(ANDROID)
  #include <android/log.h>
  #include <unistd.h> // chdir
#endif

using std::string;
using std::unique_ptr;
using std::make_unique;
using std::filesystem::path;
using std::filesystem::u8path;
using boost::optional;

namespace {

#if defined(ANDROID)
inline void logI(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    __android_log_vprint(ANDROID_LOG_INFO, "Rhubarb", fmt, args);
    va_end(args);
}
inline void logE(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    __android_log_vprint(ANDROID_LOG_ERROR, "Rhubarb", fmt, args);
    va_end(args);
}
#else
inline void logI(const char*, ...) {}
inline void logE(const char*, ...) {}
#endif

// Store resource root (folder that contains `res/`).
static std::string g_resource_root;

// Print nested exception chain (repo uses std::throw_with_nested)
static void logNested(const std::exception& e, int level = 0) {
    // Small indentation for readability
    for (int i = 0; i < level; i++) {
        logE("  ");
    }
    logE("%s", e.what());

    try {
        std::rethrow_if_nested(e);
    } catch (const std::exception& nested) {
        logNested(nested, level + 1);
    } catch (...) {
        logE("  <non-std nested exception>");
    }
}

static ShapeSet getTargetShapeSetForCapi(const string& extendedShapesString) {
    ShapeSet result(ShapeConverter::get().getBasicShapes());
    for (char ch : extendedShapesString) {
        Shape shape = ShapeConverter::get().parse(string(1, ch));
        result.insert(shape);
    }
    return result;
}

static unique_ptr<Recognizer> createRecognizerForCapi(RecognizerType recognizerType) {
    switch (recognizerType) {
        case RecognizerType::PocketSphinx:
            return make_unique<PocketSphinxRecognizer>();
        case RecognizerType::Phonetic:
            return make_unique<PhoneticRecognizer>();
        default:
            throw std::runtime_error("Unknown recognizer.");
    }
}

static string analyzeWavToJsonInternal(
    const path&          inputFilePath,
    optional<string>     dialogText,
    RecognizerType       recognizerType,
    const string&        extendedShapesString,
    int                  maxThreadCount
) {
    ShapeSet targetShapeSet = getTargetShapeSetForCapi(extendedShapesString);

    // No progress callback for now
    ProgressForwarder progressSink([](double) {});

    auto recognizer = createRecognizerForCapi(recognizerType);

    JoiningContinuousTimeline<Shape> animation = animateWaveFile(
        inputFilePath,
        dialogText,
        *recognizer,
        targetShapeSet,
        maxThreadCount,
        progressSink
    );

    JsonExporter jsonExporter;
    ExporterInput exporterInput(inputFilePath, animation, targetShapeSet);

    std::ostringstream out;
    jsonExporter.exportAnimation(exporterInput, out);
    return out.str();
}

// Ensure Rhubarb resolves "res/..." relative to an app-provided root.
// On Android, Rhubarb guesses the executable as /system/bin/app_process64,
// so we must override by chdir() to a folder that contains res/.
static void applyResourceRootIfSet() {
#if defined(ANDROID)
    if (!g_resource_root.empty()) {
        if (chdir(g_resource_root.c_str()) == 0) {
            logI("applyResourceRootIfSet: chdir(%s) ok", g_resource_root.c_str());
        } else {
            logE("applyResourceRootIfSet: chdir(%s) FAILED", g_resource_root.c_str());
        }
    } else {
        logE("applyResourceRootIfSet: resource root NOT set (call rhubarb_set_resource_root)");
    }
#endif
}

} // namespace

extern "C" {

// NEW: set resource root (folder that contains `res/`)
int rhubarb_set_resource_root(const char* root_dir_utf8) {
    if (!root_dir_utf8 || root_dir_utf8[0] == '\0') return 3;

#if defined(_WIN32)
    _putenv_s("RHUBARB_RESOURCE_ROOT", root_dir_utf8);
#else
    setenv("RHUBARB_RESOURCE_ROOT", root_dir_utf8, 1);
#endif
    return 0;
}

// Simple, file-based API.
int rhubarb_analyze_wav_file(
    const char* wav_path,
    char*       out_json,
    int         out_json_size
) {
    if (!wav_path || !out_json || out_json_size <= 0) {
        logE("rhubarb_analyze_wav_file: invalid args (wav_path=%p out_json=%p size=%d)",
             wav_path, out_json, out_json_size);
        return 3; // invalid args
    }

    try {
        logI("rhubarb_analyze_wav_file: wav_path=%s", wav_path);

        applyResourceRootIfSet();

        path inputPath = u8path(string(wav_path));

        optional<string> dialogText;              // no transcript
        RecognizerType   recognizer     = RecognizerType::Phonetic;
        string           extendedShapes = "GHX";  // CLI default
        int              maxThreads     = getProcessorCoreCount();

        logI("rhubarb_analyze_wav_file: recognizer=phonetic extendedShapes=%s threads=%d",
             extendedShapes.c_str(), maxThreads);

        string json = analyzeWavToJsonInternal(
            inputPath,
            dialogText,
            recognizer,
            extendedShapes,
            maxThreads
        );

        logI("rhubarb_analyze_wav_file: json_size=%d", (int)json.size());

        if ((int)json.size() + 1 > out_json_size) {
            logE("rhubarb_analyze_wav_file: buffer too small (need=%d have=%d)",
                 (int)json.size() + 1, out_json_size);
            return 2; // buffer too small
        }

        std::memcpy(out_json, json.c_str(), json.size() + 1);
        return 0;

    } catch (const std::exception& e) {
        logE("rhubarb_analyze_wav_file: exception chain:");
        logNested(e);
        return 1;
    } catch (...) {
        logE("rhubarb_analyze_wav_file: unknown exception");
        return 1;
    }
}

// Configurable API (Flutter-friendly).
int rhubarb_analyze_wav(
    const char* wav_path,
    const char* dialog_text,
    int         recognizer_type,
    const char* extended_shapes,
    char*       out_json,
    int         out_json_size
) {
    if (!wav_path || !out_json || out_json_size <= 0) {
        logE("rhubarb_analyze_wav: invalid args (wav_path=%p out_json=%p size=%d)",
             wav_path, out_json, out_json_size);
        return 3; // invalid args
    }

    try {
        logI("rhubarb_analyze_wav: called");
        logI("rhubarb_analyze_wav: wav_path=%s", wav_path);
        logI("rhubarb_analyze_wav: recognizer_type=%d (ignored on Android)", recognizer_type);

        applyResourceRootIfSet();

        path inputPath = u8path(string(wav_path));

        // dialog_text: optional
        optional<string> dialogText;
        if (dialog_text && dialog_text[0] != '\0') {
            dialogText = string(dialog_text);
            logI("rhubarb_analyze_wav: dialog_text_len=%d", (int)dialogText->size());
        } else {
            logI("rhubarb_analyze_wav: dialog_text=(none)");
        }

        // On Android, force phonetic mode (PocketSphinx model resources are heavy and fragile).
        // Note: even phonetic mode may still require `res/sphinx/...` for some tools.
        RecognizerType recognizer = RecognizerType::Phonetic;
        logI("rhubarb_analyze_wav: forcing phonetic recognizer (mobile-safe)");

        // extended_shapes: optional, default "GHX"
        string shapes = "GHX";
        if (extended_shapes && extended_shapes[0] != '\0') {
            shapes = string(extended_shapes);
        }
        logI("rhubarb_analyze_wav: extended_shapes=%s", shapes.c_str());

        int maxThreads = getProcessorCoreCount();
        logI("rhubarb_analyze_wav: maxThreads=%d", maxThreads);

        string json = analyzeWavToJsonInternal(
            inputPath,
            dialogText,
            recognizer,
            shapes,
            maxThreads
        );

        logI("rhubarb_analyze_wav: json_size=%d", (int)json.size());

        if ((int)json.size() + 1 > out_json_size) {
            logE("rhubarb_analyze_wav: buffer too small (need=%d have=%d)",
                 (int)json.size() + 1, out_json_size);
            return 2; // buffer too small
        }

        std::memcpy(out_json, json.c_str(), json.size() + 1);
        return 0;

    } catch (const std::exception& e) {
        logE("rhubarb_analyze_wav: exception chain:");
        logNested(e);
        return 1;
    } catch (...) {
        logE("rhubarb_analyze_wav: unknown exception");
        return 1;
    }
}

} // extern "C"
