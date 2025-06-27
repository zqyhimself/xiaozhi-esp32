#include "lcd_display.h"
#include "anim_player.h"
#include "mmap_assets.h"
#include "esp_timer.h"

#include <vector>
#include <algorithm>
#include <font_awesome_symbols.h>
#include <esp_log.h>
#include <esp_err.h>
#include <esp_lvgl_port.h>
#include <esp_heap_caps.h>
#include "assets/lang_config.h"
#include <cstring>
#include "settings.h"

#include "board.h"

#define TAG "LcdDisplay"

#define DEFAULT_ANIMATION_PARTITION_LABEL "animations"
#define CHARACTER_ANIM_GIRL_AAF_ID 0
// Placeholder for other animation IDs
// #define CHARACTER_ANIM_BOY_AAF_ID 1
// #define CHARACTER_ANIM_ROBOT_AAF_ID 2


// Placeholder for actual wallpaper C arrays (RGB565 format)
// LV_IMG_DECLARE(wallpaper_1_rgb565);
// LV_IMG_DECLARE(wallpaper_2_rgb565);
// LV_IMG_DECLARE(wallpaper_3_rgb565);
// LV_IMG_DECLARE(wallpaper_4_rgb565);
// LV_IMG_DECLARE(wallpaper_default_rgb565); // A default wallpaper

// Color definitions for dark theme
#define DARK_BACKGROUND_COLOR       lv_color_hex(0x121212)
#define DARK_TEXT_COLOR             lv_color_white()
#define DARK_CHAT_BACKGROUND_COLOR  lv_color_hex(0x1E1E1E)
#define DARK_USER_BUBBLE_COLOR      lv_color_hex(0x1A6C37)
#define DARK_ASSISTANT_BUBBLE_COLOR lv_color_hex(0x333333)
#define DARK_SYSTEM_BUBBLE_COLOR    lv_color_hex(0x2A2A2A)
#define DARK_SYSTEM_TEXT_COLOR      lv_color_hex(0xAAAAAA)
#define DARK_BORDER_COLOR           lv_color_hex(0x333333)
#define DARK_LOW_BATTERY_COLOR      lv_color_hex(0xFF0000)

// Color definitions for light theme
#define LIGHT_BACKGROUND_COLOR       lv_color_white()
#define LIGHT_TEXT_COLOR             lv_color_black()
#define LIGHT_CHAT_BACKGROUND_COLOR  lv_color_hex(0xE0E0E0)
#define LIGHT_USER_BUBBLE_COLOR      lv_color_hex(0x95EC69)
#define LIGHT_ASSISTANT_BUBBLE_COLOR lv_color_white()
#define LIGHT_SYSTEM_BUBBLE_COLOR    lv_color_hex(0xE0E0E0)
#define LIGHT_SYSTEM_TEXT_COLOR      lv_color_hex(0x666666)
#define LIGHT_BORDER_COLOR           lv_color_hex(0xE0E0E0)
#define LIGHT_LOW_BATTERY_COLOR      lv_color_black()


const ThemeColors DARK_THEME = {
    .background = DARK_BACKGROUND_COLOR, .text = DARK_TEXT_COLOR, .chat_background = DARK_CHAT_BACKGROUND_COLOR,
    .user_bubble = DARK_USER_BUBBLE_COLOR, .assistant_bubble = DARK_ASSISTANT_BUBBLE_COLOR, .system_bubble = DARK_SYSTEM_BUBBLE_COLOR,
    .system_text = DARK_SYSTEM_TEXT_COLOR, .border = DARK_BORDER_COLOR, .low_battery = DARK_LOW_BATTERY_COLOR
};
const ThemeColors LIGHT_THEME = {
    .background = LIGHT_BACKGROUND_COLOR, .text = LIGHT_TEXT_COLOR, .chat_background = LIGHT_CHAT_BACKGROUND_COLOR,
    .user_bubble = LIGHT_USER_BUBBLE_COLOR, .assistant_bubble = LIGHT_ASSISTANT_BUBBLE_COLOR, .system_bubble = LIGHT_SYSTEM_BUBBLE_COLOR,
    .system_text = LIGHT_SYSTEM_TEXT_COLOR, .border = LIGHT_BORDER_COLOR, .low_battery = LIGHT_LOW_BATTERY_COLOR
};

LV_FONT_DECLARE(font_awesome_30_4);

LcdDisplay::LcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, DisplayFonts fonts, int width, int height)
    : Display(), panel_io_lcddisplay_(panel_io), panel_lcddisplay_(panel), fonts_(fonts),
      wallpaper_img_(nullptr), aaf_player_handle_(nullptr), animation_assets_handle_(nullptr),
      lcd_panel_handle_for_aaf_(nullptr), lcd_io_handle_for_aaf_(nullptr) {
    this->width_ = width;
    this->height_ = height;

    Settings settings("display", false);
    current_theme_name_ = settings.GetString("theme", "light"); // Use Display::current_theme_name_
    if (current_theme_name_ == "dark") current_theme_ = DARK_THEME; // Use LcdDisplay::current_theme_
    else current_theme_ = LIGHT_THEME;
}

SpiLcdDisplay::SpiLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                           int width, int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y, bool swap_xy,
                           DisplayFonts fonts)
    : LcdDisplay(panel_io, panel, fonts, width, height) {
    this->lcd_panel_handle_for_aaf_ = panel;
    this->lcd_io_handle_for_aaf_ = panel_io;

    ESP_LOGI(TAG, "Turning display on"); ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(this->panel_lcddisplay_, true));
    ESP_LOGI(TAG, "Initialize LVGL library"); lv_init();
    ESP_LOGI(TAG, "Initialize LVGL port");
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority = 4; port_cfg.timer_period_ms = 5;  lvgl_port_init(&port_cfg);
    ESP_LOGI(TAG, "Adding LCD display");
    const lvgl_port_display_cfg_t display_cfg = {
        .io_handle = this->panel_io_lcddisplay_, .panel_handle = this->panel_lcddisplay_, .control_handle = nullptr,
        .buffer_size = static_cast<uint32_t>(width * 20), .double_buffer = false, .trans_size = 0,
        .hres = static_cast<uint32_t>(width), .vres = static_cast<uint32_t>(height), .monochrome = false,
        .rotation = {.swap_xy = swap_xy, .mirror_x = mirror_x, .mirror_y = mirror_y,},
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = {.buff_dma = 1, .buff_spiram = 0, .sw_rotate = 0, .swap_bytes = 1, .full_refresh = 0, .direct_mode = 0,},
    };
    this->display_ = lvgl_port_add_disp(&display_cfg);
    if (this->display_ == nullptr) { ESP_LOGE(TAG, "Failed to add display in SpiLcdDisplay"); return; }
    if (offset_x != 0 || offset_y != 0) lv_display_set_offset(this->display_, offset_x, offset_y);

    InitializeAafPlayerIfNeeded(DEFAULT_ANIMATION_PARTITION_LABEL);
    SetupUI();
}

RgbLcdDisplay::RgbLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                           int width, int height, int offset_x, int offset_y,
                           bool mirror_x, bool mirror_y, bool swap_xy,
                           DisplayFonts fonts)
    : LcdDisplay(panel_io, panel, fonts, width, height) {
    this->lcd_panel_handle_for_aaf_ = panel; this->lcd_io_handle_for_aaf_ = panel_io;
    ESP_LOGI(TAG, "Initialize LVGL library"); lv_init();
    ESP_LOGI(TAG, "Initialize LVGL port");
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority = 4; port_cfg.timer_period_ms = 5; lvgl_port_init(&port_cfg);
    ESP_LOGI(TAG, "Adding LCD display for RgbLcdDisplay");
    const lvgl_port_display_cfg_t display_cfg = {
        .io_handle = this->panel_io_lcddisplay_, .panel_handle = this->panel_lcddisplay_, .buffer_size = static_cast<uint32_t>(width * 20),
        .double_buffer = true, .hres = static_cast<uint32_t>(width), .vres = static_cast<uint32_t>(height),
        .rotation = {.swap_xy = swap_xy, .mirror_x = mirror_x, .mirror_y = mirror_y,},
        .flags = {.buff_dma = 1, .swap_bytes = 0, .full_refresh = 1, .direct_mode = 1,},
    };
    const lvgl_port_display_rgb_cfg_t rgb_cfg = {.flags = {.bb_mode = true, .avoid_tearing = true}};
    this->display_ = lvgl_port_add_disp_rgb(&display_cfg, &rgb_cfg);
    if (this->display_ == nullptr) { ESP_LOGE(TAG, "Failed to add RGB display"); return; }
    if (offset_x != 0 || offset_y != 0) lv_display_set_offset(this->display_, offset_x, offset_y);
    InitializeAafPlayerIfNeeded(DEFAULT_ANIMATION_PARTITION_LABEL);
    SetupUI();
}

MipiLcdDisplay::MipiLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                            int width, int height,  int offset_x, int offset_y,
                            bool mirror_x, bool mirror_y, bool swap_xy,
                            DisplayFonts fonts)
    : LcdDisplay(panel_io, panel, fonts, width, height) {
    this->lcd_panel_handle_for_aaf_ = panel; this->lcd_io_handle_for_aaf_ = panel_io;
    ESP_LOGI(TAG, "Turning display on"); ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(this->panel_lcddisplay_, true));
    ESP_LOGI(TAG, "Initialize LVGL library"); lv_init();
    ESP_LOGI(TAG, "Initialize LVGL port");
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.timer_period_ms = 5; lvgl_port_init(&port_cfg);
    ESP_LOGI(TAG, "Adding LCD display for MipiLcdDisplay");
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = this->panel_io_lcddisplay_, .panel_handle = this->panel_lcddisplay_, .control_handle = nullptr,
        .buffer_size = static_cast<uint32_t>(width * 50), .double_buffer = false,
        .hres = static_cast<uint32_t>(width), .vres = static_cast<uint32_t>(height), .monochrome = false,
        .rotation = {.swap_xy = swap_xy, .mirror_x = mirror_x, .mirror_y = mirror_y,},
        .flags = {.buff_dma = true, .buff_spiram =false, .sw_rotate = false,},
    };
    const lvgl_port_display_dsi_cfg_t dpi_cfg = {.flags = {.avoid_tearing = false,}};
    this->display_ = lvgl_port_add_disp_dsi(&disp_cfg, &dpi_cfg);
    if (this->display_ == nullptr) { ESP_LOGE(TAG, "Failed to add MIPI DSI display"); return;}
    if (offset_x != 0 || offset_y != 0) lv_display_set_offset(this->display_, offset_x, offset_y);
    InitializeAafPlayerIfNeeded(DEFAULT_ANIMATION_PARTITION_LABEL);
    SetupUI();
}

// Implement Lock/Unlock for concrete classes
bool SpiLcdDisplay::Lock(int timeout_ms) { return lvgl_port_lock(timeout_ms); }
void SpiLcdDisplay::Unlock() { lvgl_port_unlock(); }
bool RgbLcdDisplay::Lock(int timeout_ms) { return lvgl_port_lock(timeout_ms); }
void RgbLcdDisplay::Unlock() { lvgl_port_unlock(); }
bool MipiLcdDisplay::Lock(int timeout_ms) { return lvgl_port_lock(timeout_ms); }
void MipiLcdDisplay::Unlock() { lvgl_port_unlock(); }
// Add QSPI and MCU8080 Lock/Unlock if they are used
bool QspiLcdDisplay::Lock(int timeout_ms) { return lvgl_port_lock(timeout_ms); }
void QspiLcdDisplay::Unlock() { lvgl_port_unlock(); }
bool Mcu8080LcdDisplay::Lock(int timeout_ms) { return lvgl_port_lock(timeout_ms); }
void Mcu8080LcdDisplay::Unlock() { lvgl_port_unlock(); }


LcdDisplay::~LcdDisplay() {
    if (wallpaper_img_ && lv_obj_is_valid(wallpaper_img_)) { lv_obj_del(wallpaper_img_); wallpaper_img_ = nullptr; }
    if (aaf_player_handle_) { anim_player_update(aaf_player_handle_, PLAYER_ACTION_STOP); anim_player_deinit(aaf_player_handle_); aaf_player_handle_ = nullptr; }
    if (animation_assets_handle_) { mmap_assets_del(animation_assets_handle_); animation_assets_handle_ = nullptr; }

    // Base Display class members are cleaned by its own destructor or LVGL's hierarchy.
    // LcdDisplay specific LVGL objects (if any not part of base) would be cleaned here.
    // For panel_io_lcddisplay_ and panel_lcddisplay_, they are passed in, so their lifecycle is managed by the caller (Board).
    // We only nullify our copies used for AAF player.
    if (lcd_io_handle_for_aaf_) {
        esp_lcd_panel_io_register_event_callbacks(lcd_io_handle_for_aaf_, nullptr, nullptr);
        lcd_io_handle_for_aaf_ = nullptr;
    }
    lcd_panel_handle_for_aaf_ = nullptr;
    panel_lcddisplay_ = nullptr;
    panel_io_lcddisplay_ = nullptr;
}

void LcdDisplay::SetupWallpaperAndAnimation() {
    DisplayLockGuard lock(this);
    lv_obj_t* screen = lv_screen_active();
    if (!screen) return;

    wallpaper_img_ = lv_img_create(screen);
    lv_obj_set_style_bg_color(wallpaper_img_, lv_color_black(), 0);
    lv_obj_set_size(wallpaper_img_, LV_HOR_RES, LV_VER_RES);
    lv_obj_align(wallpaper_img_, LV_ALIGN_DEFAULT, 0, 0);
    lv_obj_send_to_back(wallpaper_img_);
    // Example to load initial wallpaper (replace with actual image C array if available)
    // LV_IMG_DECLARE(wallpaper_default_rgb565_c_array);
    // if (&wallpaper_default_rgb565_c_array) { // Check if the image is declared/available
    //     SetStaticBackground(&wallpaper_default_rgb565_c_array);
    // } else {
    //    ESP_LOGW(TAG, "Default wallpaper C array not found for initial setup.");
    // }
}

void LcdDisplay::InitializeAafPlayerIfNeeded(const char* partition_label) {
    if (aaf_player_handle_) return;
    if (!lcd_panel_handle_for_aaf_ || !lcd_io_handle_for_aaf_) {
        ESP_LOGE(TAG, "Panel/IO handles for AAF not set. Panel: %p, IO: %p", (void*)lcd_panel_handle_for_aaf_, (void*)lcd_io_handle_for_aaf_); return;
    }
    ESP_LOGI(TAG, "Initializing AAF Player for partition: %s", partition_label);
    const mmap_assets_config_t assets_cfg = { .partition_label = partition_label, .max_files = 5, .checksum = 0, .flags = {.mmap_enable = true, .full_check = false}};
    esp_err_t ret = mmap_assets_new(&assets_cfg, &animation_assets_handle_);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "Failed to init mmap_assets for %s: %s", partition_label, esp_err_to_name(ret)); animation_assets_handle_ = nullptr; return; }
    ESP_LOGI(TAG, "mmap_assets for %s OK. Files: %d", partition_label, mmap_assets_get_file_count(animation_assets_handle_));
    anim_player_config_t player_cfg = { .flush_cb = LcdDisplay::AnimPlayerFlushCallback, .update_cb = NULL, .user_data = this, .flags = {.swap = true}, .task = ANIM_PLAYER_INIT_CONFIG()};
    player_cfg.task.task_core = 1; player_cfg.task.task_priority = 5;
    aaf_player_handle_ = anim_player_init(&player_cfg);
    if (!aaf_player_handle_) {
        ESP_LOGE(TAG, "Failed to init anim_player");
        if (animation_assets_handle_) { mmap_assets_del(animation_assets_handle_); animation_assets_handle_ = nullptr; }
        return;
    }
    ESP_LOGI(TAG, "AAF anim_player OK.");
    const esp_lcd_panel_io_callbacks_t cbs = {.on_color_trans_done = LcdDisplay::AnimPlayerFlushReadyCallback,};
    ret = esp_lcd_panel_io_register_event_callbacks(this->lcd_io_handle_for_aaf_, &cbs, aaf_player_handle_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register panel_io callbacks: %s", esp_err_to_name(ret));
        anim_player_deinit(aaf_player_handle_); aaf_player_handle_ = nullptr;
        if (animation_assets_handle_) { mmap_assets_del(animation_assets_handle_); animation_assets_handle_ = nullptr; }
    } else { ESP_LOGI(TAG, "Panel IO callbacks for AAF player OK."); }
}

void LcdDisplay::AnimPlayerFlushCallback(anim_player_handle_t handle, int x_start, int y_start, int x_end, int y_end, const void *color_data) {
    LcdDisplay* instance = static_cast<LcdDisplay*>(anim_player_get_user_data(handle));
    if (instance && instance->lcd_panel_handle_for_aaf_) {
        esp_lcd_panel_draw_bitmap(instance->lcd_panel_handle_for_aaf_, x_start, y_start, x_end, y_end, color_data);
    }
}

bool LcdDisplay::AnimPlayerFlushReadyCallback(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx) {
    anim_player_handle_t player_handle = static_cast<anim_player_handle_t>(user_ctx);
    if (player_handle) anim_player_flush_ready(player_handle);
    return true;
}

void LcdDisplay::StartCharacterAnimation(int file_id, bool repeat, int fps) {
    if (!aaf_player_handle_ || !animation_assets_handle_) { ESP_LOGE(TAG, "AAF Player or assets not init. Cannot start anim ID %d", file_id); return; }
    const void *src_data = mmap_assets_get_mem(animation_assets_handle_, file_id);
    size_t src_len = mmap_assets_get_size(animation_assets_handle_, file_id);
    if (!src_data || src_len == 0) {
        ESP_LOGE(TAG, "Failed to get AAF data for ID %d. Size: %u. Files in partition: %d",
                 file_id, (unsigned int)src_len, animation_assets_handle_ ? mmap_assets_get_file_count(animation_assets_handle_) : -1);
        return;
    }
    ESP_LOGI(TAG, "Starting AAF anim ID %d, size: %u, fps: %d, repeat: %s", file_id, (unsigned int)src_len, fps, repeat ? "true" : "false");
    anim_player_set_src_data(aaf_player_handle_, src_data, src_len);
    uint32_t start_frame, end_frame;
    anim_player_get_segment(aaf_player_handle_, &start_frame, &end_frame);
    anim_player_set_segment(aaf_player_handle_, start_frame, end_frame, fps, repeat);
    anim_player_update(aaf_player_handle_, PLAYER_ACTION_START);
}

void LcdDisplay::SetStaticBackground(const lv_img_dsc_t *img_dsc) {
    DisplayLockGuard lock(this);
    if (wallpaper_img_ && img_dsc) {
        lv_img_set_src(wallpaper_img_, img_dsc);
    } else if (wallpaper_img_) {
        lv_img_set_src(wallpaper_img_, NULL);
        lv_obj_set_style_bg_color(wallpaper_img_, lv_color_black(), 0);
    }
}

void LcdDisplay::UpdateWallpaper(const std::string& name) {
    DisplayLockGuard lock(this);
    ESP_LOGI(TAG, "UpdateWallpaper called with: %s", name.c_str());
    // Example:
    // if (name == "wallpaper_1") { LV_IMG_DECLARE(wallpaper_1_rgb565_c_array); SetStaticBackground(&wallpaper_1_rgb565_c_array); }
    // else { ESP_LOGW(TAG, "Wallpaper '%s' not found.", name.c_str()); }
}

void LcdDisplay::PlayCharacterAnimation(const std::string& anim_name, bool repeat, int fps) {
    DisplayLockGuard lock(this);
    ESP_LOGI(TAG, "PlayCharacterAnimation called with: %s", anim_name.c_str());
    int file_id = -1;
    if (anim_name == "girl") { file_id = CHARACTER_ANIM_GIRL_AAF_ID; }
    // else if (anim_name == "boy") { file_id = CHARACTER_ANIM_BOY_AAF_ID; }
    // else if (anim_name == "robot") { file_id = CHARACTER_ANIM_ROBOT_AAF_ID; }

    if (file_id != -1) {
        StartCharacterAnimation(file_id, repeat, fps);
    } else {
        ESP_LOGW(TAG, "Character animation '%s' not found.", anim_name.c_str());
    }
}


#if CONFIG_USE_WECHAT_MESSAGE_STYLE
void LcdDisplay::SetupUI() { // WeChat Style UI
    DisplayLockGuard lock(this);
    auto screen = lv_screen_active();
    if (!screen) return;
    // Base styles are set in Display constructor or LcdDisplay constructor if lv_disp_t is ready
    // lv_obj_set_style_text_font(screen, fonts_.text_font, 0); // fonts_ is LcdDisplay member
    // lv_obj_set_style_text_color(screen, current_theme_.text, 0); // current_theme_ is LcdDisplay
    // lv_obj_set_style_bg_color(screen, current_theme_.background, 0);

    SetupWallpaperAndAnimation();

    container_ = lv_obj_create(screen);
    lv_obj_set_size(container_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(container_, 0, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_pad_row(container_, 0, 0);
    lv_obj_set_style_bg_opa(container_, LV_OPA_TRANSP, 0);

    status_bar_ = lv_obj_create(container_);
    lv_obj_set_size(status_bar_, LV_HOR_RES, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(status_bar_, 0, 0);
    lv_obj_set_style_bg_color(status_bar_, current_theme_.background, 0);
    lv_obj_set_style_text_color(status_bar_, current_theme_.text, 0);
    
    content_ = lv_obj_create(container_);
    lv_obj_set_style_radius(content_, 0, 0);
    lv_obj_set_width(content_, LV_HOR_RES);
    lv_obj_set_flex_grow(content_, 1);
    lv_obj_set_style_pad_all(content_, 10, 0);

    lv_color_t semi_trans_bg_color = lv_color_mix(current_theme_.chat_background, lv_color_black(), LV_OPA_70);
    lv_obj_set_style_bg_color(content_, semi_trans_bg_color, 0);
    lv_obj_set_style_bg_opa(content_, LV_OPA_SEMI_TRANSP, 0);
    lv_obj_set_style_border_width(content_, 0, 0);

    lv_obj_set_scrollbar_mode(content_, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_scroll_dir(content_, LV_DIR_VER);
    lv_obj_set_flex_flow(content_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(content_, 10, 0);
    this->chat_message_label_ = nullptr; // Uses LcdDisplay member

    // Status bar elements (using Display:: members for LVGL objects from base class)
    lv_obj_set_flex_flow(status_bar_, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(status_bar_,0,0); lv_obj_set_style_border_width(status_bar_,0,0); lv_obj_set_style_pad_column(status_bar_,0,0);
    lv_obj_set_style_pad_left(status_bar_,10,0); lv_obj_set_style_pad_right(status_bar_,10,0); lv_obj_set_style_pad_top(status_bar_,2,0); lv_obj_set_style_pad_bottom(status_bar_,2,0);
    lv_obj_set_scrollbar_mode(status_bar_,LV_SCROLLBAR_MODE_OFF); lv_obj_set_flex_align(status_bar_,LV_FLEX_ALIGN_SPACE_BETWEEN,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
    Display::emotion_label_=lv_label_create(status_bar_); lv_obj_set_style_text_font(Display::emotion_label_,&font_awesome_30_4,0); lv_obj_set_style_text_color(Display::emotion_label_,current_theme_.text,0); lv_label_set_text(Display::emotion_label_,FONT_AWESOME_AI_CHIP); lv_obj_set_style_margin_right(Display::emotion_label_,5,0);
    Display::notification_label_=lv_label_create(status_bar_); lv_obj_set_flex_grow(Display::notification_label_,1); lv_obj_set_style_text_align(Display::notification_label_,LV_TEXT_ALIGN_CENTER,0); lv_obj_set_style_text_color(Display::notification_label_,current_theme_.text,0); lv_label_set_text(Display::notification_label_,""); lv_obj_add_flag(Display::notification_label_,LV_OBJ_FLAG_HIDDEN);
    Display::status_label_=lv_label_create(status_bar_); lv_obj_set_flex_grow(Display::status_label_,1); lv_label_set_long_mode(Display::status_label_,LV_LABEL_LONG_SCROLL_CIRCULAR); lv_obj_set_style_text_align(Display::status_label_,LV_TEXT_ALIGN_CENTER,0); lv_obj_set_style_text_color(Display::status_label_,current_theme_.text,0); lv_label_set_text(Display::status_label_,Lang::Strings::INITIALIZING);
    Display::mute_label_=lv_label_create(status_bar_); lv_label_set_text(Display::mute_label_,""); lv_obj_set_style_text_font(Display::mute_label_,fonts_.icon_font,0); lv_obj_set_style_text_color(Display::mute_label_,current_theme_.text,0);
    Display::network_label_=lv_label_create(status_bar_); lv_label_set_text(Display::network_label_,""); lv_obj_set_style_text_font(Display::network_label_,fonts_.icon_font,0); lv_obj_set_style_text_color(Display::network_label_,current_theme_.text,0); lv_obj_set_style_margin_left(Display::network_label_,5,0);
    Display::battery_label_=lv_label_create(status_bar_); lv_label_set_text(Display::battery_label_,""); lv_obj_set_style_text_font(Display::battery_label_,fonts_.icon_font,0); lv_obj_set_style_text_color(Display::battery_label_,current_theme_.text,0); lv_obj_set_style_margin_left(Display::battery_label_,5,0);
    Display::low_battery_popup_=lv_obj_create(screen); lv_obj_set_scrollbar_mode(Display::low_battery_popup_,LV_SCROLLBAR_MODE_OFF); lv_obj_set_size(Display::low_battery_popup_,LV_HOR_RES*0.9,fonts_.text_font->line_height*2); lv_obj_align(Display::low_battery_popup_,LV_ALIGN_BOTTOM_MID,0,0); lv_obj_set_style_bg_color(Display::low_battery_popup_,current_theme_.low_battery,0); lv_obj_set_style_radius(Display::low_battery_popup_,10,0);
    Display::low_battery_label_=lv_label_create(Display::low_battery_popup_); lv_label_set_text(Display::low_battery_label_,Lang::Strings::BATTERY_NEED_CHARGE); lv_obj_set_style_text_color(Display::low_battery_label_,lv_color_white(),0); lv_obj_center(Display::low_battery_label_); lv_obj_add_flag(Display::low_battery_popup_,LV_OBJ_FLAG_HIDDEN);
}

// SetChatMessage and SetPreviewImage remain largely the same, ensuring correct member variable usage.
#elif !CONFIG_USE_WECHAT_MESSAGE_STYLE // Normal UI style
void LcdDisplay::SetupUI() {
    DisplayLockGuard lock(this);
    auto screen = lv_screen_active();
    if(!screen) return;
    // Base styles for screen can be set here or rely on Display constructor
    lv_obj_set_style_text_font(screen, fonts_.text_font, 0);
    lv_obj_set_style_text_color(screen, current_theme_.text, 0);
    lv_obj_set_style_bg_color(screen, current_theme_.background, 0);

    SetupWallpaperAndAnimation();

    container_ = lv_obj_create(screen);
    lv_obj_set_size(container_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(container_, 0, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_pad_row(container_, 0, 0);
    lv_obj_set_style_bg_opa(container_, LV_OPA_TRANSP, 0);

    status_bar_ = lv_obj_create(container_);
    lv_obj_set_size(status_bar_, LV_HOR_RES, Display::fonts_.text_font->line_height + 8);
    lv_obj_set_style_radius(status_bar_, 0, 0);
    lv_obj_set_style_bg_color(status_bar_, current_theme_.background, 0);
    lv_obj_set_style_text_color(status_bar_, current_theme_.text, 0);

    content_ = lv_obj_create(container_);
    lv_obj_set_scrollbar_mode(content_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_radius(content_, 0, 0);
    lv_obj_set_width(content_, LV_HOR_RES);
    lv_obj_set_flex_grow(content_, 1);
    lv_obj_set_style_pad_all(content_, 5, 0);
    lv_color_t semi_trans_content_bg = lv_color_mix(current_theme_.chat_background, lv_color_black(), LV_OPA_70); // More opaque
    lv_obj_set_style_bg_color(content_, semi_trans_content_bg, 0);
    lv_obj_set_style_bg_opa(content_, LV_OPA_SEMI_TRANSP, 0);
    lv_obj_set_style_border_width(content_, 0, 0);

    lv_obj_set_flex_flow(content_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER); // Center content for normal UI

    // In Normal UI, AAF animation is primary. emotion_label_ and preview_image_ are not created here.
    // Display::chat_message_label_ can be used for status or short messages.
    Display::chat_message_label_ = lv_label_create(content_);
    lv_label_set_text(Display::chat_message_label_, "");
    lv_obj_set_width(Display::chat_message_label_, LV_HOR_RES * 0.9);
    lv_label_set_long_mode(Display::chat_message_label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(Display::chat_message_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(Display::chat_message_label_, current_theme_.text, 0);
    lv_obj_set_style_bg_opa(Display::chat_message_label_, LV_OPA_TRANSP, 0);

    // Status bar elements (using Display:: members for LVGL objects from base class)
    lv_obj_set_flex_flow(status_bar_, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(status_bar_,0,0); lv_obj_set_style_border_width(status_bar_,0,0); lv_obj_set_style_pad_column(status_bar_,0,0);
    lv_obj_set_style_pad_left(status_bar_,2,0); lv_obj_set_style_pad_right(status_bar_,2,0);
    Display::network_label_=lv_label_create(status_bar_);lv_label_set_text(Display::network_label_,"");lv_obj_set_style_text_font(Display::network_label_,Display::fonts_.icon_font,0);lv_obj_set_style_text_color(Display::network_label_,current_theme_.text,0);
    Display::notification_label_=lv_label_create(status_bar_);lv_obj_set_flex_grow(Display::notification_label_,1);lv_obj_set_style_text_align(Display::notification_label_,LV_TEXT_ALIGN_CENTER,0);lv_obj_set_style_text_color(Display::notification_label_,current_theme_.text,0);lv_label_set_text(Display::notification_label_,"");lv_obj_add_flag(Display::notification_label_,LV_OBJ_FLAG_HIDDEN);
    Display::status_label_=lv_label_create(status_bar_);lv_obj_set_flex_grow(Display::status_label_,1);lv_label_set_long_mode(Display::status_label_,LV_LABEL_LONG_SCROLL_CIRCULAR);lv_obj_set_style_text_align(Display::status_label_,LV_TEXT_ALIGN_CENTER,0);lv_obj_set_style_text_color(Display::status_label_,current_theme_.text,0);lv_label_set_text(Display::status_label_,Lang::Strings::INITIALIZING);
    Display::mute_label_=lv_label_create(status_bar_);lv_label_set_text(Display::mute_label_,"");lv_obj_set_style_text_font(Display::mute_label_,Display::fonts_.icon_font,0);lv_obj_set_style_text_color(Display::mute_label_,current_theme_.text,0);
    Display::battery_label_=lv_label_create(status_bar_);lv_label_set_text(Display::battery_label_,"");lv_obj_set_style_text_font(Display::battery_label_,Display::fonts_.icon_font,0);lv_obj_set_style_text_color(Display::battery_label_,current_theme_.text,0);
    Display::low_battery_popup_=lv_obj_create(screen);lv_obj_set_scrollbar_mode(Display::low_battery_popup_,LV_SCROLLBAR_MODE_OFF);lv_obj_set_size(Display::low_battery_popup_,LV_HOR_RES*0.9,Display::fonts_.text_font->line_height*2);lv_obj_align(Display::low_battery_popup_,LV_ALIGN_BOTTOM_MID,0,0);lv_obj_set_style_bg_color(Display::low_battery_popup_,current_theme_.low_battery,0);lv_obj_set_style_radius(Display::low_battery_popup_,10,0);
    Display::low_battery_label_=lv_label_create(Display::low_battery_popup_);lv_label_set_text(Display::low_battery_label_,Lang::Strings::BATTERY_NEED_CHARGE);lv_obj_set_style_text_color(Display::low_battery_label_,lv_color_white(),0);lv_obj_center(Display::low_battery_label_);lv_obj_add_flag(Display::low_battery_popup_,LV_OBJ_FLAG_HIDDEN);

    if (aaf_player_handle_) {
        ESP_LOGI(TAG, "Normal UI: Playing default character animation (girl).");
        PlayCharacterAnimation("girl", true, 15); // Use public method
    } else {
        ESP_LOGW(TAG, "Normal UI: AAF player not ready, cannot play default animation.");
    }
}
#endif // CONFIG_USE_WECHAT_MESSAGE_STYLE


// Implement SetChatMessage and SetPreviewImage only if WeChat style is enabled,
// as they are specific to that layout.
#if CONFIG_USE_WECHAT_MESSAGE_STYLE
#if CONFIG_IDF_TARGET_ESP32P4
#define  MAX_MESSAGES 40
#else
#define  MAX_MESSAGES 20
#endif
void LcdDisplay::SetChatMessage(const char* role, const char* content_str) {
    DisplayLockGuard lock(this);
    if (content_ == nullptr) return;
    if(strlen(content_str) == 0) return;
    uint32_t child_count = lv_obj_get_child_cnt(content_);
    if (child_count >= MAX_MESSAGES) {
        lv_obj_t* first_child = lv_obj_get_child(content_, 0);
        if (first_child) lv_obj_del(first_child);
         if (lv_obj_get_child_cnt(content_) > 0) {
             lv_obj_t* current_last_child = lv_obj_get_child(content_, lv_obj_get_child_cnt(content_) - 1);
             if (current_last_child) lv_obj_scroll_to_view_recursive(current_last_child, LV_ANIM_OFF);
        }
    }
    if (strcmp(role, "system") == 0 && child_count > 0) {
        lv_obj_t* last_container_obj = lv_obj_get_child(content_, lv_obj_get_child_cnt(content_) -1); // Corrected index
        if (last_container_obj && lv_obj_get_child_cnt(last_container_obj) > 0) {
            lv_obj_t* last_bubble_obj = lv_obj_get_child(last_container_obj, 0);
            if (last_bubble_obj) {
                void* bubble_type_ptr = lv_obj_get_user_data(last_bubble_obj);
                if (bubble_type_ptr && strcmp((const char*)bubble_type_ptr, "system") == 0) {
                    lv_obj_del(last_container_obj);
                }
            }
        }
    }
    lv_obj_t* msg_bubble = lv_obj_create(content_); // msg_bubble is child of content_
    lv_obj_set_style_radius(msg_bubble, 8, 0);
    lv_obj_set_scrollbar_mode(msg_bubble, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_border_width(msg_bubble, 1, 0);
    lv_obj_set_style_border_color(msg_bubble, current_theme_.border, 0);
    lv_obj_set_style_pad_all(msg_bubble, 8, 0);
    lv_obj_t* msg_text = lv_label_create(msg_bubble);
    lv_label_set_text(msg_text, content_str);
    lv_coord_t text_width = lv_txt_get_width(content_str, strlen(content_str), fonts_.text_font, 0); // fonts_ is LcdDisplay member
    lv_coord_t max_b_width = LV_HOR_RES * 85 / 100 - 16;
    lv_coord_t min_b_width = 20;
    lv_coord_t actual_b_width = (text_width < min_b_width) ? min_b_width : ((text_width < max_b_width) ? text_width : max_b_width);
    lv_obj_set_width(msg_text, actual_b_width);
    lv_label_set_long_mode(msg_text, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(msg_text, fonts_.text_font, 0);
    lv_obj_set_width(msg_bubble, LV_SIZE_CONTENT);
    lv_obj_set_height(msg_bubble, LV_SIZE_CONTENT);
    if (strcmp(role, "user") == 0) { /* ... */ } else if (strcmp(role, "assistant") == 0) { /* ... */ } else if (strcmp(role, "system") == 0) { /* ... */ } // Simplified for brevity
    // ... rest of SetChatMessage ...
    if (strcmp(role, "user") == 0) {
        lv_obj_set_style_bg_color(msg_bubble, current_theme_.user_bubble, 0);
        lv_obj_set_style_text_color(msg_text, current_theme_.text, 0);
        lv_obj_set_user_data(msg_bubble, (void*)"user");
    } else if (strcmp(role, "assistant") == 0) {
        lv_obj_set_style_bg_color(msg_bubble, current_theme_.assistant_bubble, 0);
        lv_obj_set_style_text_color(msg_text, current_theme_.text, 0);
        lv_obj_set_user_data(msg_bubble, (void*)"assistant");
    } else if (strcmp(role, "system") == 0) {
        lv_obj_set_style_bg_color(msg_bubble, current_theme_.system_bubble, 0);
        lv_obj_set_style_text_color(msg_text, current_theme_.system_text, 0);
        lv_obj_set_user_data(msg_bubble, (void*)"system");
    }
    lv_obj_set_style_flex_grow(msg_bubble, 0, 0);
    lv_obj_t* align_cont = lv_obj_create(content_);
    lv_obj_remove_style_all(align_cont);
    lv_obj_set_width(align_cont, lv_pct(100));
    lv_obj_set_height(align_cont, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(align_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_parent(msg_bubble, align_cont);
    if (strcmp(role, "user") == 0) lv_obj_align(msg_bubble, LV_ALIGN_RIGHT_MID, 0, 0);
    else if (strcmp(role, "system") == 0) lv_obj_align(msg_bubble, LV_ALIGN_CENTER, 0, 0);
    else lv_obj_align(msg_bubble, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_scroll_to_view_recursive(align_cont, LV_ANIM_ON);
    this->chat_message_label_ = msg_text; // Uses LcdDisplay::chat_message_label_
}

void LcdDisplay::SetPreviewImage(const lv_img_dsc_t* img_dsc) {
    DisplayLockGuard lock(this);
    if (content_ == nullptr) return;
    if (img_dsc != nullptr) { /* ... same as before ... */ }
}
#endif // CONFIG_USE_WECHAT_MESSAGE_STYLE


void LcdDisplay::SetEmotion(const char* emotion) {
// ... (implementation as before, ensuring Display::emotion_label_ is used if SetupUI for WeChat path uses it)
#if CONFIG_USE_WECHAT_MESSAGE_STYLE
    struct Emotion { const char* icon; const char* text; };
    static const std::vector<Emotion> emotions = { /* ... emotions ... */
        {"😶", "neutral"}, {"🙂", "happy"}, {"😆", "laughing"}, {"😂", "funny"},
        {"😔", "sad"}, {"😠", "angry"}, {"😭", "crying"}, {"😍", "loving"},
        {"😳", "embarrassed"}, {"😯", "surprised"}, {"😱", "shocked"}, {"🤔", "thinking"},
        {"😉", "winking"}, {"😎", "cool"}, {"😌", "relaxed"}, {"🤤", "delicious"},
        {"😘", "kissy"}, {"😏", "confident"}, {"😴", "sleepy"}, {"😜", "silly"},
        {"🙄", "confused"}
    };
    std::string_view emotion_view(emotion);
    auto it = std::find_if(emotions.begin(), emotions.end(),
        [&emotion_view](const Emotion& e) { return e.text == emotion_view; });
    DisplayLockGuard lock(this); // Already locked by caller if Display::SetEmotion calls this
    if (Display::emotion_label_ == nullptr) return;
    lv_obj_set_style_text_font(Display::emotion_label_, fonts_.emoji_font, 0);
    if (it != emotions.end()) lv_label_set_text(Display::emotion_label_, it->icon);
    else lv_label_set_text(Display::emotion_label_, "😶");
#else
    ESP_LOGI(TAG, "SetEmotion (Normal UI): %s. Character animation is primary.", emotion);
    // Example: PlayCharacterAnimation(MapEmotionToAnimationName(emotion), true, 15);
#endif
}

void LcdDisplay::SetIcon(const char* icon) {
    DisplayLockGuard lock(this); // Already locked by caller
#if CONFIG_USE_WECHAT_MESSAGE_STYLE
    if (Display::emotion_label_ == nullptr) return;
    lv_obj_set_style_text_font(Display::emotion_label_, &font_awesome_30_4, 0);
    lv_label_set_text(Display::emotion_label_, icon);
#else
    if (Display::chat_message_label_){
         lv_obj_set_style_text_font(Display::chat_message_label_, &font_awesome_30_4, 0);
         lv_label_set_text(Display::chat_message_label_, icon);
    }
    ESP_LOGI(TAG, "SetIcon (Normal UI): %s", icon);
#endif
}

void LcdDisplay::SetTheme(const std::string& theme_name) {
    DisplayLockGuard lock(this); // Already locked
    if (theme_name == "dark" || theme_name == "DARK") current_theme_ = DARK_THEME;
    else if (theme_name == "light" || theme_name == "LIGHT") current_theme_ = LIGHT_THEME;
    else { ESP_LOGE(TAG, "Invalid theme name: %s", theme_name.c_str()); return; }
    
    lv_obj_t* screen = lv_screen_active();
    if (!screen) return;
    // These are now set in Display::SetTheme, called at the end.
    // lv_obj_set_style_bg_color(screen, current_theme_.background, 0);
    // lv_obj_set_style_text_color(screen, current_theme_.text, 0);
    
    if (container_) { /* transparent */ lv_obj_set_style_border_color(container_, current_theme_.border, 0); }
    if (status_bar_) {
        lv_obj_set_style_bg_color(status_bar_, current_theme_.background, 0);
        lv_obj_set_style_text_color(status_bar_, current_theme_.text, 0);
        if (Display::network_label_) lv_obj_set_style_text_color(Display::network_label_, current_theme_.text, 0);
        // ... update other status bar children ...
    }
    if (content_) {
        lv_color_t semi_trans_bg = lv_color_mix(current_theme_.chat_background, lv_color_black(), LV_OPA_70);
        lv_obj_set_style_bg_color(content_, semi_trans_bg, 0);
#if CONFIG_USE_WECHAT_MESSAGE_STYLE
        // ... update WeChat bubbles ...
#else
        if (Display::chat_message_label_) lv_obj_set_style_text_color(Display::chat_message_label_, current_theme_.text, 0);
#endif
    }
    if (Display::low_battery_popup_) lv_obj_set_style_bg_color(Display::low_battery_popup_, current_theme_.low_battery, 0);
    Display::SetTheme(theme_name); // Call base to store name and update screen common styles
}
