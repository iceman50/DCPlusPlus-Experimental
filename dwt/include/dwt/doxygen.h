/*  Definitions for doxygen
	Copyright (c) 2026 iceman50
	(Never included by c++ compilers)
*/

/** \defgroup WidgetControls Widgets
  */

/** \defgroup aspects::Classes Aspect Classes
  */

/** \defgroup GlobalStuff Global typedefs and functions
  * Global functions and typedefs or things in general not belonging to any
  * particular class.
  */

/** \defgroup WidgetLayout Layout of widgets in windows
  * \htmlinclude layout.html
  */

/** \defgroup SubclassDialog Dialog Subclassing Functions
  */

/**
  * \defgroup eventsSignatures  Event Signatures
  *
  * See MessageMapControl.h
  *
*/


/** \defgroup EventHandlersaspects::Clickable Event Handlers for aspects::Clickable
  * \EventHandlerGenericInfo
  */

/** \defgroup EventHandlersaspects::DblClickable Event Handlers for aspects::DblClickable
  * \EventHandlerGenericInfo
  */

/** \defgroup EventHandlersaspects::Keyboard Event Handlers for aspects::Keyboard
  * \EventHandlerGenericInfo
  */

/** \defgroup EventHandlersaspects::Mouse Event Handlers for aspects::Mouse
  * \EventHandlerGenericInfo
  */

/** \defgroup EventHandlersaspects::Painting Event Handlers for aspects::Painting
  * \EventHandlerGenericInfo
  */

/** \defgroup EventHandlersaspects::Selection Event Handlers for aspects::Selection
  * \EventHandlerGenericInfo
  */

/** \defgroup EventHandlersaspects::Sizable Event Handlers for aspects::Sizable
  * \EventHandlerGenericInfo
  */

/** \defgroup EventHandlersaspects::Enabled Event Handlers for aspects::Enabled
  * \EventHandlerGenericInfo
  */

/** \defgroup EventHandlersaspects::EraseBackground Event Handlers for aspects::EraseBackground
  * \EventHandlerGenericInfo
  */

/** \defgroup EventHandlersaspects::Activate Event Handlers for aspects::Activate
  * \EventHandlerGenericInfo
  */

/** \defgroup EventHandlersaspects::BackgroundColor Event Handlers for aspects::BackgroundColor
  * \EventHandlerGenericInfo
  */

/** \defgroup EventHandlersaspects::AspectFocus Event Handlers for aspects::Focus
  * \EventHandlerGenericInfo
  */

/** \defgroup EventHandlersaspects::Scrollable Event Handlers for aspects::Scrollable
  * \EventHandlerGenericInfo
  */

/** \defgroup EventHandlersaspects::Text Event Handlers for aspects::Text
  * \EventHandlerGenericInfo
  */

/** \defgroup EventHandlersaspects::Update Event Handlers for aspects::Update
  * \EventHandlerGenericInfo
  */

/** \defgroup EventHandlersaspects::Visible Event Handlers for aspects::Visible
  * \EventHandlerGenericInfo
  */

/** \defgroup EventHandlersaspects::RightClickable Event Handlers for aspects::RightClickable
  * \EventHandlerGenericInfo
  */

/** \defgroup EventHandlersTable Event Handlers for Table
  * \EventHandlerGenericInfo
  */

/** \defgroup EventHandlersMenu Event Handlers for Menu
  * \EventHandlerGenericInfo
  */

/** \defgroup EventHandlersToolBar Event Handlers for ToolBar
  * \EventHandlerGenericInfo
  */

/** \defgroup EventHandlersTree Event Handlers for Tree
  * \EventHandlerGenericInfo
  */

/** \defgroup Appearance Application appearance
  * Application-wide light, dark, and high-contrast behavior.
  */

/** \page appearance Configuring application appearance
  *
  * DWT retains native window behavior, metrics, input, and accessibility while
  * manually painting framework chrome in dark mode or with an explicit custom
  * palette. The renderer is available on Windows 7 and later and does not depend
  * on an operating-system dark visual style. Windows high contrast always takes
  * precedence over the selected mode and palette.
  *
  * Configure the service once on the GUI thread, normally before creating the main
  * window:
  * \code
  * auto palette = dwt::Appearance::defaultPalette();
  * palette.accent = RGB(0, 120, 215);
  * dwt::Application::instance().getAppearance().configure(
  *     dwt::Appearance::Mode::Dark, palette);
  * \endcode
  *
  * The two-argument configure() keeps automatic rendering: manual when dark,
  * native when light. To opt into the same manual renderers with a light palette,
  * explicitly select CustomPalette (provide all seven palette colors):
  * \code
  * dwt::Appearance::Palette lightPalette = {
  *     RGB(245, 245, 245), RGB(255, 255, 255), RGB(25, 25, 25),
  *     RGB(110, 110, 110), RGB(175, 175, 175), RGB(0, 90, 180),
  *     RGB(255, 255, 255)
  * };
  * dwt::Application::instance().getAppearance().configure(
  *     dwt::Appearance::Mode::Light, lightPalette,
  *     dwt::Appearance::Rendering::CustomPalette);
  * \endcode
  * Mode still determines isDark(); palette brightness is not inferred. System mode
  * follows the Windows light/dark preference without changing the supplied palette.
  * Calling the two-argument configure() again restores automatic rendering.
  *
  * Widgets participate automatically. Use dwt::AppearancePolicy::ApplicationContent
  * for a surface whose application-defined formatting must be preserved, or call
  * setColor() to provide explicit foreground and background colors. Calling
  * clearColor() returns a colorable widget to appearance inheritance.
  *
  * Top-level dwt::Frame windows replace the native caption and menu bar with
  * client-area controls in manual mode. Layout application controls inside
  * Frame::getContentRect(), which excludes the caption, menu rows, and resize
  * gutters. getClientSize() continues to report the complete Win32 client area.
  * Use Frame::adjustContentRect() when sizing a window around its content and
  * Frame::setMenuBar() when showing or hiding an attached menu. Menu::setMenu()
  * also updates the client frame. Automatic light mode, high contrast, and the Native
  * appearance policy restore the original native frame styles.
  *
  * To retain Windows' caption, borders, hit testing, and system menu while using
  * palette-painted controls, opt in per window (before creation or live):
  * \code
  * window->setFrameRendering(dwt::Frame::FrameRendering::Native);
  * \endcode
  * The default FrameRendering::Automatic keeps the behavior described above.
  * Native framing attempts the palette's surface, text and border colors through
  * DWM when manual rendering is active. Unsupported attributes retain Windows'
  * native appearance. High contrast, AppearancePolicy::Native, automatic light
  * mode, and returning to Automatic framing reset DWT-applied DWM styling.
  * This option belongs to DWT Frame windows; native common-dialog adaptation
  * continues to use its existing scoped rendering policy.
  *
  * DWM access and SDK compatibility declarations live in util/win32/Dwm.h.
  * UxTheme access lives in util/win32/UxTheme.h; Theme continues to own theme
  * handles and their reload/close lifecycle. Baseline UxTheme calls remain linked
  * to the system library, preserving the existing Windows 7 behavior.
  *
  * \section appearance_live Updating an existing application
  *
  * Apply a theme on the GUI thread. When an application also owns fonts, brushes,
  * row colors, or formatted text, update those resources in the same transaction
  * as the framework palette:
  * \code
  * auto& appearance = dwt::Application::instance().getAppearance();
  * {
  *     dwt::Appearance::UpdateScope update(appearance);
  *     // Publish application-owned style definitions and rebuild font caches here.
  *     appearance.configure(dwt::Appearance::Mode::Light, lightPalette,
  *         dwt::Appearance::Rendering::CustomPalette);
  *     // Update any explicit content colors and invalidate custom drawing here.
  * }
  * // The outermost scope presents the completed theme.
  * \endcode
  *
  * configure() applies the palette to existing widgets and invokes onChanged()
  * observers synchronously. UpdateScope nests with that internal update and
  * defers DWT's final synchronous repaint until the outermost scope ends. It
  * does not suspend arbitrary Win32 drawing or asynchronous application work;
  * do not pump messages or open modal dialogs halfway through an update.
  *
  * Use onChanged() when application-owned content must also respond to Windows
  * appearance changes. Save its returned iterator and call removeChanged()
  * before destroying objects captured by the callback. Do not call configure()
  * from that callback. For custom drawing, respect isHighContrast() and use the
  * service's content colors when Windows high contrast is active.
  *
  * Cached rows should resolve visual overrides from the current application
  * theme on repaint. Waiting for a network update or batching timer to replace
  * copied row colors can leave old colors visible after the rest of the window
  * has switched. Keep user matching and data updates separate from resolving
  * their current visual style. DWT does not own these application data models.
  *
  * \section appearance_interfaces Choosing the customization interface
  *
  * - Appearance owns the application palette, rendering preference, high-contrast
  *   policy, widget refresh, and change notifications.
  * - Frame::setFrameRendering() chooses native or automatic framing for a DWT
  *   top-level window. Prefer it to setting raw DWM attributes on that window:
  *   Frame reapplies its own attributes on subsequent appearance changes.
  * - Theme owns an HTHEME used to draw native visual-style parts. load() normally
  *   installs a WM_THEMECHANGED callback; reload() reopens the stored class and
  *   unload() closes its handle. Keep the Theme object alive while its widget
  *   can invoke that callback. Check the Theme before drawing and provide a
  *   fallback when a visual style is unavailable.
  * - util::win32::NativeDialogAppearance adapts native modal dialogs. Keep its
  *   scope alive throughout the blocking show call. DWT dialog wrappers already
  *   create this scope; applications using raw native dialog calls can create
  *   one themselves. The scope captures the current palette when constructed.
  * - util::win32::dwm and util::win32::uxtheme provide access to Windows APIs.
  *   They do not select application colors, own widget lifetimes, or decide
  *   high-contrast behavior.
  *
  * \section appearance_platform Platform access and fallbacks
  *
  * Keep DWM entry-point lookup and newer SDK attribute constants in Dwm.h/.cpp.
  * The utility resolves the library and exports once on demand. Missing exports
  * return E_NOTIMPL; otherwise the original HRESULT reaches the caller. Check
  * each optional attribute independently: success for one attribute does not
  * establish support for another. Frame's native mode retains native framing
  * when an attribute fails instead of switching renderers.
  *
  * The dwm::defaultColor sentinel restores Windows' color choice. The
  * resetFrameAppearance() helper attempts dark-mode, caption, text, and border
  * resets even if some fail, then reports the first failure. It does not reset
  * unrelated attributes, such as non-client rendering policy or taskbar preview
  * settings. Callers of the low-level setters own their restoration policy.
  *
  * Keep baseline visual-style calls in UxTheme.h/.cpp. These wrappers preserve
  * return values, rectangle arguments, text flags, and null versus empty theme
  * names. They do not add drawing fallbacks or take ownership of supplied theme
  * handles. Add any future version-dependent entry points here with an explicit
  * unavailable-API result; keep widget painting and input behavior at the caller.
  */


/** \page dwt Documentation for DWT
  * \htmlinclude guide.html
  */

/**
  * \page license SmartWin++ license
  * \htmlinclude license.html
*/

/**
  * \page msvc71  MS Visual C++ 7.1
  * \htmlinclude msvc71.html
*/

/**
  * \page devcpp  DEV C++
  * \htmlinclude devcpp.html
*/

/**
  * \page msvctoolkit  MSVC 2003 toolkit
  * \htmlinclude msvcToolkit.html
*/

/**
  * \page eclipse  Eclipse C++ Development Environment
  * \htmlinclude eclipse.html
*/

/**
  * \page linuxwine  Using Winelib to run on Linux
  * \htmlinclude linuxwine.html
*/

/**
  * \page samplecode1  Hello World (small)
   *
   * This is a program to show the basics of SmartWin++
   *
   * \include main.cpp
   *
   * This program compiles into an executable of 155 KB without any extra DLLs needed.
   * Here is a screen shot of two instances running:
   *
   * \image html hello.jpg
   *
   * The two message boxes either say "Hello !" or "Hello World !" depending on the
   * Global checkbox.  The layout logic puts the button either to the right or below
   * the checkbox depending on the window shape.
   *
   * Notice the way events are handled in SmartWin++: you specify a member function
   * that executes when the event happens.
   *
   * Notice also that the button and the checkbox's size are set once.  But their
   * runtime position depends on how the window is resized by the user. The resource
   * editor was not used in this sample. although SmartWin++ can use the MSVC
   * resource editor if you want to.
   *
   * <a href="samplecode2.html"> Here is a larger example </a>
   * There are 40 other source code samples.
*/

/**
  * \page samplecode2  Hello World (big)
   * This is a program to show most of the features being used together.
   *
   * \include hello.cpp
   *
   * This program compiles into an executable of 268 KB without any extra DLLs needed.
   *
   * \image html helloSmartWin.jpg
*/
