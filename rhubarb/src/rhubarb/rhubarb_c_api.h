#pragma once

#ifdef __cplusplus
extern "C" {
#endif

    int rhubarb_set_resource_root(const char* root_dir_utf8);

    // Simple file-based API (original behavior).
    //
    // Return codes:
    //  0 = success
    //  1 = internal error
    //  2 = output buffer too small
    //  3 = invalid arguments (reserved for consistency)
    int rhubarb_analyze_wav_file(
        const char* wav_path,
        char*       out_json,
        int         out_json_size
    );

    // More configurable API.
    //
    // Parameters:
    //  wav_path        - required, UTF-8 path to a WAV file
    //  dialog_text     - optional, may be NULL or "" (no transcript)
    //  recognizer_type - 0 = phonetic, 1 = PocketSphinx
    //  extended_shapes - optional, NULL or "" => default "GHX"
    //  out_json        - output buffer for JSON (UTF-8)
    //  out_json_size   - size of out_json in bytes
    //
    // Return codes:
    //  0 = success
    //  1 = internal error
    //  2 = output buffer too small
    //  3 = invalid arguments
    int rhubarb_analyze_wav(
        const char* wav_path,
        const char* dialog_text,
        int         recognizer_type,
        const char* extended_shapes,
        char*       out_json,
        int         out_json_size
    );

#ifdef __cplusplus
}
#endif
