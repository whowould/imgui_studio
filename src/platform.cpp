#include "platform.h"

#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include <d3d11.h>
#include <algorithm>
#include <dwmapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <windows.h>
#include <windowsx.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam );

namespace
{
    HWND window = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    IDXGISwapChain* swap_chain = nullptr;
    ID3D11RenderTargetView* target = nullptr;
    UINT resize_width = 0, resize_height = 0;
    bool running = true;

    void create_target( )
    {
        ID3D11Texture2D* back = nullptr;
        if ( SUCCEEDED( swap_chain->GetBuffer( 0, IID_PPV_ARGS( &back ) ) ) )
        {
            device->CreateRenderTargetView( back, nullptr, &target );
            back->Release( );
        }
    }

    void release_target( )
    {
        if ( target )
        {
            target->Release( );
            target = nullptr;
        }
    }

    auto create_device( ) -> bool
    {
        DXGI_SWAP_CHAIN_DESC desc{ };
        desc.BufferCount = 2;
        desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.OutputWindow = window;
        desc.SampleDesc.Count = 1;
        desc.Windowed = TRUE;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        constexpr D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
        D3D_FEATURE_LEVEL level;
        HRESULT result = D3D11CreateDeviceAndSwapChain( nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2, D3D11_SDK_VERSION,
                                                        &desc, &swap_chain, &device, &level, &context );
        if ( result == DXGI_ERROR_UNSUPPORTED )
            result = D3D11CreateDeviceAndSwapChain( nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &desc,
                                                    &swap_chain, &device, &level, &context );
        if ( FAILED( result ) )
            return false;
        create_target( );
        return true;
    }

    // Resize edges for a borderless window; the rest of the client area is ours.
    auto hit_test( HWND hwnd, LPARAM lparam ) -> LRESULT
    {
        POINT cursor{ GET_X_LPARAM( lparam ), GET_Y_LPARAM( lparam ) };
        RECT rect;
        GetWindowRect( hwnd, &rect );
        if ( IsZoomed( hwnd ) )
            return HTCLIENT;
        const int edge = 6;
        const bool left = cursor.x < rect.left + edge, right = cursor.x >= rect.right - edge;
        const bool top = cursor.y < rect.top + edge, bottom = cursor.y >= rect.bottom - edge;
        if ( top && left ) return HTTOPLEFT;
        if ( top && right ) return HTTOPRIGHT;
        if ( bottom && left ) return HTBOTTOMLEFT;
        if ( bottom && right ) return HTBOTTOMRIGHT;
        if ( left ) return HTLEFT;
        if ( right ) return HTRIGHT;
        if ( top ) return HTTOP;
        if ( bottom ) return HTBOTTOM;
        return HTCLIENT;
    }

    LRESULT CALLBACK procedure( HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam )
    {
        if ( ImGui_ImplWin32_WndProcHandler( hwnd, message, wparam, lparam ) )
            return true;
        switch ( message )
        {
        case WM_NCCALCSIZE:
            if ( wparam )
            {
                // No system frame; when maximized, keep the client inside the monitor's work area.
                if ( IsZoomed( hwnd ) )
                {
                    auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>( lparam );
                    const int frame = GetSystemMetrics( SM_CXFRAME ) + GetSystemMetrics( SM_CXPADDEDBORDER );
                    params->rgrc[0].left += frame;
                    params->rgrc[0].top += frame;
                    params->rgrc[0].right -= frame;
                    params->rgrc[0].bottom -= frame;
                }
                return 0;
            }
            break;
        case WM_NCHITTEST:
            return hit_test( hwnd, lparam );
        case WM_GETMINMAXINFO:
        {
            auto* info = reinterpret_cast<MINMAXINFO*>( lparam );
            info->ptMinTrackSize = { 960, 600 };
            return 0;
        }
        case WM_SIZE:
            if ( wparam != SIZE_MINIMIZED )
            {
                resize_width = LOWORD( lparam );
                resize_height = HIWORD( lparam );
            }
            return 0;
        case WM_SYSCOMMAND:
            if ( ( wparam & 0xfff0 ) == SC_KEYMENU )
                return 0;
            break;
        case WM_DESTROY:
            running = false;
            PostQuitMessage( 0 );
            return 0;
        default:
            break;
        }
        return DefWindowProcW( hwnd, message, wparam, lparam );
    }
}

auto platform::create( const wchar_t* title, int width, int height ) -> bool
{
    ImGui_ImplWin32_EnableDpiAwareness( );
    WNDCLASSEXW wc{ sizeof( wc ), CS_CLASSDC, procedure, 0, 0, GetModuleHandleW( nullptr ), nullptr, LoadCursorW( nullptr, IDC_ARROW ),
                    nullptr, nullptr, L"imgui_studio", nullptr };
    RegisterClassExW( &wc );
    const float scale = ImGui_ImplWin32_GetDpiScaleForMonitor( MonitorFromPoint( { 0, 0 }, MONITOR_DEFAULTTOPRIMARY ) );
    // The requested size is a maximum: the window takes up to 92% of the work area, centred in it.
    RECT area{ 0, 0, GetSystemMetrics( SM_CXSCREEN ), GetSystemMetrics( SM_CYSCREEN ) };
    SystemParametersInfoW( SPI_GETWORKAREA, 0, &area, 0 );
    const int area_w = area.right - area.left, area_h = area.bottom - area.top;
    const int w = ( std::min )( static_cast<int>( width * scale ), area_w * 92 / 100 );
    const int h = ( std::min )( static_cast<int>( height * scale ), area_h * 92 / 100 );
    const int x = area.left + ( area_w - w ) / 2, y = area.top + ( area_h - h ) / 2;
    window = CreateWindowExW( WS_EX_APPWINDOW, wc.lpszClassName, title, WS_OVERLAPPEDWINDOW, x, y, w, h, nullptr, nullptr, wc.hInstance,
                              nullptr );
    if ( !window )
        return false;

    // Dark, rounded Windows 11 chrome with a subtle border; harmless on older systems.
    const BOOL dark = TRUE;
    DwmSetWindowAttribute( window, 20, &dark, sizeof( dark ) );
    const int round = 2;
    DwmSetWindowAttribute( window, 33, &round, sizeof( round ) );
    const COLORREF border = RGB( 46, 46, 52 );
    DwmSetWindowAttribute( window, 34, &border, sizeof( border ) );
    const MARGINS margins{ 0, 0, 1, 0 };
    DwmExtendFrameIntoClientArea( window, &margins );
    SetWindowPos( window, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER );

    if ( !create_device( ) )
        return false;
    ShowWindow( window, SW_SHOWDEFAULT );
    UpdateWindow( window );

    IMGUI_CHECKVERSION( );
    ImGui::CreateContext( );
    ImGuiIO& io = ImGui::GetIO( );
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // The editor is laid out in pixels like the menus it builds, so fonts aren't DPI-scaled.
    ImGui_ImplWin32_Init( window );
    ImGui_ImplDX11_Init( device, context );
    CoInitializeEx( nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE );
    return true;
}

void platform::destroy( )
{
    ImGui_ImplDX11_Shutdown( );
    ImGui_ImplWin32_Shutdown( );
    ImGui::DestroyContext( );
    release_target( );
    if ( swap_chain ) swap_chain->Release( );
    if ( context ) context->Release( );
    if ( device ) device->Release( );
    if ( window ) DestroyWindow( window );
    UnregisterClassW( L"imgui_studio", GetModuleHandleW( nullptr ) );
}

auto platform::pump( ) -> bool
{
    MSG msg;
    while ( PeekMessageW( &msg, nullptr, 0, 0, PM_REMOVE ) )
    {
        TranslateMessage( &msg );
        DispatchMessageW( &msg );
        if ( msg.message == WM_QUIT )
            running = false;
    }
    if ( resize_width && resize_height && running )
    {
        release_target( );
        swap_chain->ResizeBuffers( 0, resize_width, resize_height, DXGI_FORMAT_UNKNOWN, 0 );
        resize_width = resize_height = 0;
        create_target( );
    }
    return running;
}

void platform::begin_frame( )
{
    ImGui_ImplDX11_NewFrame( );
    ImGui_ImplWin32_NewFrame( );
    ImGui::NewFrame( );
}

void platform::end_frame( ImVec4 clear )
{
    ImGui::Render( );
    const float color[4] = { clear.x, clear.y, clear.z, clear.w };
    context->OMSetRenderTargets( 1, &target, nullptr );
    context->ClearRenderTargetView( target, color );
    ImGui_ImplDX11_RenderDrawData( ImGui::GetDrawData( ) );
    swap_chain->Present( 1, 0 );
}

void platform::drag_window( )
{
    ReleaseCapture( );
    SendMessageW( window, WM_NCLBUTTONDOWN, HTCAPTION, 0 );
}

void platform::minimize( )
{
    ShowWindow( window, SW_MINIMIZE );
}

void platform::toggle_maximize( )
{
    ShowWindow( window, IsZoomed( window ) ? SW_RESTORE : SW_MAXIMIZE );
}

auto platform::maximized( ) -> bool
{
    return IsZoomed( window ) != 0;
}

void platform::close( )
{
    PostMessageW( window, WM_CLOSE, 0, 0 );
}

void platform::set_title( const std::wstring& title )
{
    SetWindowTextW( window, title.c_str( ) );
}

namespace
{
    struct filter_t
    {
        const wchar_t* name;
        const wchar_t* pattern;
        const wchar_t* extension;
    };
    constexpr filter_t projects{ L"ImGui Studio project", L"*.imstudio", L"imstudio" };
    constexpr filter_t images{ L"Images", L"*.png;*.jpg;*.jpeg;*.bmp;*.gif", nullptr };
    constexpr filter_t libraries{ L"Widget plugin", L"*.dll", L"dll" };

    auto dialog( bool save, const std::wstring& suggested, bool folder, const filter_t& filter = projects ) -> std::wstring
    {
        IFileDialog* picker = nullptr;
        if ( FAILED( CoCreateInstance( save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog, nullptr, CLSCTX_ALL,
                                       IID_PPV_ARGS( &picker ) ) ) )
            return { };
        DWORD options = 0;
        picker->GetOptions( &options );
        picker->SetOptions( options | FOS_FORCEFILESYSTEM | ( folder ? FOS_PICKFOLDERS : 0 ) );
        if ( !folder )
        {
            const COMDLG_FILTERSPEC types[] = { { filter.name, filter.pattern }, { L"All files", L"*.*" } };
            picker->SetFileTypes( 2, types );
            if ( filter.extension )
                picker->SetDefaultExtension( filter.extension );
            if ( !suggested.empty( ) )
                picker->SetFileName( suggested.c_str( ) );
        }
        std::wstring result;
        if ( SUCCEEDED( picker->Show( window ) ) )
        {
            IShellItem* item = nullptr;
            if ( SUCCEEDED( picker->GetResult( &item ) ) )
            {
                PWSTR path = nullptr;
                if ( SUCCEEDED( item->GetDisplayName( SIGDN_FILESYSPATH, &path ) ) )
                {
                    result = path;
                    CoTaskMemFree( path );
                }
                item->Release( );
            }
        }
        picker->Release( );
        return result;
    }
}

auto platform::open_file( ) -> std::wstring
{
    return dialog( false, { }, false );
}

auto platform::open_image( ) -> std::wstring
{
    return dialog( false, { }, false, images );
}

auto platform::open_library( ) -> std::wstring
{
    return dialog( false, { }, false, libraries );
}

auto platform::save_file( const std::wstring& suggested ) -> std::wstring
{
    return dialog( true, suggested, false );
}

auto platform::pick_folder( ) -> std::wstring
{
    return dialog( false, { }, true );
}

auto platform::app_data( ) -> std::wstring
{
    PWSTR roaming = nullptr;
    std::wstring folder;
    if ( SUCCEEDED( SHGetKnownFolderPath( FOLDERID_RoamingAppData, 0, nullptr, &roaming ) ) )
    {
        folder = std::wstring( roaming ) + L"\\ImGui Studio";
        CoTaskMemFree( roaming );
        CreateDirectoryW( folder.c_str( ), nullptr );
    }
    return folder;
}

void platform::copy_text( const std::string& text )
{
    ImGui::SetClipboardText( text.c_str( ) );
}

auto platform::widen( const std::string& text ) -> std::wstring
{
    if ( text.empty( ) )
        return { };
    const int size = MultiByteToWideChar( CP_UTF8, 0, text.data( ), static_cast<int>( text.size( ) ), nullptr, 0 );
    std::wstring out( static_cast<size_t>( size ), L'\0' );
    MultiByteToWideChar( CP_UTF8, 0, text.data( ), static_cast<int>( text.size( ) ), out.data( ), size );
    return out;
}

auto platform::narrow( const std::wstring& text ) -> std::string
{
    if ( text.empty( ) )
        return { };
    const int size = WideCharToMultiByte( CP_UTF8, 0, text.data( ), static_cast<int>( text.size( ) ), nullptr, 0, nullptr, nullptr );
    std::string out( static_cast<size_t>( size ), '\0' );
    WideCharToMultiByte( CP_UTF8, 0, text.data( ), static_cast<int>( text.size( ) ), out.data( ), size, nullptr, nullptr );
    return out;
}
