#ifndef LCD_DISPLAY_H
#define LCD_DISPLAY_H

#include "display.h"

#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <font_emoji.h> // Already present, good.

#include <atomic> // Already present
#include <string> // Already present for std::string in base
#include "anim_player.h"
#include "mmap_assets.h"

// Theme color structure
struct ThemeColors {
    lv_color_t background;
    lv_color_t text;
    lv_color_t chat_background;
    lv_color_t user_bubble;
    lv_color_t assistant_bubble;
    lv_color_t system_bubble;
    lv_color_t system_text;
    lv_color_t border;
    lv_color_t low_battery;
};


class LcdDisplay : public Display {
// panel_io_ and panel_ are already in base Display, but specific LCD types might need them directly.
// Let's ensure they are accessible if needed, or remove if base class versions are sufficient.
// For now, assume base class's panel_io_ and panel_ are not what we need for lcd_panel_handle_ etc.
// We will use new members specific to LcdDisplay for clarity with AAF player.
protected:
    esp_lcd_panel_io_handle_t panel_io_lcddisplay_ = nullptr; // Specific to LcdDisplay, distinct from Display::panel_io_ if any
    esp_lcd_panel_handle_t panel_lcddisplay_ = nullptr;   // Specific to LcdDisplay

    // Wallpaper and Animation members
    lv_obj_t* wallpaper_img_ = nullptr;
    anim_player_handle_t aaf_player_handle_ = nullptr;
    mmap_assets_handle_t animation_assets_handle_ = nullptr;
    // lcd_panel_handle_ and lcd_io_handle_ will store copies of panel_lcddisplay_ and panel_io_lcddisplay_
    // for use by anim_player callbacks, ensuring correct handles are passed.
    esp_lcd_panel_handle_t lcd_panel_handle_for_aaf_ = nullptr;
    esp_lcd_panel_io_handle_t lcd_io_handle_for_aaf_ = nullptr;

    // DisplayFonts fonts_; // This is in base Display
    // ThemeColors current_theme_; // This is in base Display

    void SetupUI(); // Common UI setup
    // Lock and Unlock are pure virtual in base, must be implemented by concrete LCD types (Spi, Rgb etc)
    // virtual bool Lock(int timeout_ms = 0) override; // Implement in Spi/Rgb/MipiLcdDisplay
    // virtual void Unlock() override; // Implement in Spi/Rgb/MipiLcdDisplay

    // Wallpaper and Animation specific protected methods
    void SetupWallpaperAndAnimation();
    void InitializeAafPlayerIfNeeded(const char* partition_label);
    void StartCharacterAnimation(int file_id, bool repeat, int fps);
    void SetStaticBackground(const lv_img_dsc_t *img_dsc);

    // Callbacks for anim_player
    static void AnimPlayerFlushCallback(anim_player_handle_t handle, int x_start, int y_start, int x_end, int y_end, const void *color_data);
    static bool AnimPlayerFlushReadyCallback(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx);

protected:
    // Constructor used by derived classes
    LcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, DisplayFonts fonts, int width, int height);
    
public:
    ~LcdDisplay();

    // Override methods from Display base class
    // SetEmotion, SetIcon, SetPreviewImage, SetChatMessage, SetTheme are already virtual in base.
    // We will override them here.
    virtual void SetEmotion(const char* emotion) override;
    virtual void SetIcon(const char* icon) override;
    virtual void SetPreviewImage(const lv_img_dsc_t* img_dsc) override;
#if CONFIG_USE_WECHAT_MESSAGE_STYLE
    virtual void SetChatMessage(const char* role, const char* content) override; 
#endif  
    virtual void SetTheme(const std::string& theme_name) override;

    // Implementations for wallpaper and animation control from Display interface
    virtual void UpdateWallpaper(const std::string& name) override;
    virtual void PlayCharacterAnimation(const std::string& anim_name, bool repeat, int fps) override;
};

// Concrete LCD implementation classes
class RgbLcdDisplay : public LcdDisplay {
public:
    RgbLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                  int width, int height, int offset_x, int offset_y,
                  bool mirror_x, bool mirror_y, bool swap_xy,
                  DisplayFonts fonts);
protected:
    virtual bool Lock(int timeout_ms = 0) override;
    virtual void Unlock() override;
};

class MipiLcdDisplay : public LcdDisplay {
public:
    MipiLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                   int width, int height, int offset_x, int offset_y,
                   bool mirror_x, bool mirror_y, bool swap_xy,
                   DisplayFonts fonts);
protected:
    virtual bool Lock(int timeout_ms = 0) override;
    virtual void Unlock() override;
};

class SpiLcdDisplay : public LcdDisplay {
public:
    SpiLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                  int width, int height, int offset_x, int offset_y,
                  bool mirror_x, bool mirror_y, bool swap_xy,
                  DisplayFonts fonts);
protected:
    virtual bool Lock(int timeout_ms = 0) override;
    virtual void Unlock() override;
};

// QSPI and MCU8080 would also need Lock/Unlock overrides
class QspiLcdDisplay : public LcdDisplay {
public:
    QspiLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                   int width, int height, int offset_x, int offset_y,
                   bool mirror_x, bool mirror_y, bool swap_xy,
                   DisplayFonts fonts);
protected:
    virtual bool Lock(int timeout_ms = 0) override;
    virtual void Unlock() override;
};

class Mcu8080LcdDisplay : public LcdDisplay {
public:
    Mcu8080LcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                      int width, int height, int offset_x, int offset_y,
                      bool mirror_x, bool mirror_y, bool swap_xy,
                      DisplayFonts fonts);
protected:
    virtual bool Lock(int timeout_ms = 0) override;
    virtual void Unlock() override;
};
#endif // LCD_DISPLAY_H
