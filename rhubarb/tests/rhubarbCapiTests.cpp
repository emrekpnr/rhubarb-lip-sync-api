#include <gtest/gtest.h>
#include "rhubarb/rhubarb_c_api.h"
#include <string>

static const char* kTestWavPath =
    "C:/Users/Emre/Repositories/talking_avatar/assets/recording.wav";

TEST(RhubarbCapi, AnalyzeWavFile_ReturnsJson) {
    char buffer[64 * 1024];

    int rc = rhubarb_analyze_wav_file(
        kTestWavPath,
        buffer,
        static_cast<int>(sizeof(buffer))
    );

    EXPECT_EQ(rc, 0) << "Expected success from rhubarb_analyze_wav_file";

    std::string json(buffer);
    EXPECT_FALSE(json.empty());
    EXPECT_NE(json.find("\"mouthCues\""), std::string::npos);
    EXPECT_NE(json.find("\"metadata\""), std::string::npos);
}

TEST(RhubarbCapi, AnalyzeWav_ReturnsJson_WithDialogAndParams) {
    char buffer[64 * 1024];

    const char* dialog =
        "A rainbow is a meteorological phenomenon that is caused by reflection, "
        "refraction and dispersion of light in water droplets resulting in a "
        "spectrum of light appearing in the sky.";

    int rc = rhubarb_analyze_wav(
        kTestWavPath,
        dialog,          // dialog_text
        0,               // recognizer_type: phonetic
        "GHX",           // extended_shapes
        buffer,
        static_cast<int>(sizeof(buffer))
    );

    EXPECT_EQ(rc, 0) << "Expected success from rhubarb_analyze_wav";

    std::string json(buffer);
    EXPECT_FALSE(json.empty());
    EXPECT_NE(json.find("\"mouthCues\""), std::string::npos);
}

TEST(RhubarbCapi, AnalyzeWav_BufferTooSmall) {
    char tinyBuffer[8];

    int rc = rhubarb_analyze_wav_file(
        kTestWavPath,
        tinyBuffer,
        static_cast<int>(sizeof(tinyBuffer))
    );

    EXPECT_EQ(rc, 2) << "Expected buffer-too-small return code";
}
