#pragma once

// Application icon (RT_GROUP_ICON); lowest id so Explorer/taskbar use it for the exe.
#define IDI_APPICON   1

// Embedded default background images (RT_RCDATA). See resources/app.rc.
#define IDR_BG_DARK   101   // ss.webp,  default dark-mode background
#define IDR_BG_LIGHT  102   // ss2.webp, default light-mode background
#define IDR_LOGO      103   // Icon.webp, in-app logo

// Embedded UI icons (RT_RCDATA), white-on-transparent PNGs tinted at draw time.
// Source: Lucide (opensvg.dev), rasterized by tools/iconbuild. Keep in sync with ui::Icon.
#define IDR_ICON_FIRST     200
#define IDR_ICON_SEARCH    200
#define IDR_ICON_FILE      201
#define IDR_ICON_FOLDER    202
#define IDR_ICON_FOLDER_OPEN 203
#define IDR_ICON_CHEVRON_UP   204
#define IDR_ICON_CHEVRON_DOWN 205
#define IDR_ICON_ARROW_UP     206
#define IDR_ICON_ARROW_DOWN   207
#define IDR_ICON_SETTINGS  208
#define IDR_ICON_SHARE     209
#define IDR_ICON_PLAY      210
#define IDR_ICON_PLUS      211
#define IDR_ICON_PACKAGE   212
#define IDR_ICON_BOX       213
#define IDR_ICON_GRIP      214
#define IDR_ICON_CLOSE     215
#define IDR_ICON_TRASH     216
#define IDR_ICON_KEY       217
#define IDR_ICON_COPY      218
#define IDR_ICON_CHECK     219
#define IDR_ICON_DOWNLOAD  220
#define IDR_ICON_UPLOAD    221
#define IDR_ICON_LINK      222
#define IDR_ICON_CIRCLE_CHECK 223
#define IDR_ICON_ALERT     224
#define IDR_ICON_HARD_DRIVE 225
#define IDR_ICON_SEND      226
#define IDR_ICON_FILE_DOWN 227
#define IDR_ICON_FILE_UP   228
#define IDR_ICON_WIFI      229
#define IDR_ICON_CHEVRON_LEFT 230
#define IDR_ICON_LAST      230
