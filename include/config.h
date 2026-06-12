#ifndef EBOOK_READER_CONFIG_H
#define EBOOK_READER_CONFIG_H

// Reading colour modes.
typedef enum {
    ColorModeLight = 0,
    ColorModeDark  = 1,
    ColorModeNight = 2   // warm "sepia"/yellowish, easy on the eyes
} ColorMode;

// Current reading colour mode.
extern int configColorMode;

// Kept for backwards compatibility: true when the UI should use its
// dark styling (i.e. dark OR night mode). Derived from configColorMode.
// Read/written through the helpers below so old call sites keep working.
#ifdef __cplusplus
extern "C" {
#endif

int  config_is_dark_ui(void);          // 1 if dark-styled UI (dark or night)
void config_cycle_color_mode(void);    // light -> dark -> night -> light

#ifdef __cplusplus
}
#endif

// Legacy alias. Existing code reads/writes `configDarkMode` as a bool;
// we keep a real variable in sync with configColorMode so nothing breaks.
extern bool configDarkMode;

extern char* configFile;

#endif
