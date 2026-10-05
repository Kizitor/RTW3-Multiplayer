#include "ui.h"
#include "game.h"
#include "delphi.h"
#include "net.h"
#include "session.h"
#include <algorithm>
#include <commctrl.h>

namespace ui {

enum {
    IDC_NAME = 1001, IDC_IP, IDC_PORT, IDC_PW,
    IDC_HOST = 1010, IDC_JOIN, IDC_FIND, IDC_LEAVE,
    IDC_ADDRS = 1020, IDC_STATUS,
    IDC_PLAYERS = 1030,
    IDC_NATION = 1040, IDC_CLAIM,
    IDC_LIMIT = 1050, IDC_SETLIMIT, IDC_LIMITLBL,
    IDC_START = 1060, IDC_ADVANCE, IDC_RELEASE,
    IDC_CHATLOG = 1070, IDC_CHATIN, IDC_SEND,
    IDC_DIPLOLBL = 1080, IDC_DIPLO, IDC_DWAR, IDC_DALLY, IDC_DPEACE, IDC_DLEAVE, IDC_DCANCEL,
    IDC_STARTMENU = 0x4D50,
};

static HINSTANCE g_inst;
static HWND g_wnd = nullptr;
static HFONT g_font = nullptr, g_bold = nullptr, g_menuFont = nullptr;
static HWND g_menuBtn = nullptr;
static WNDPROC g_chatInOld = nullptr;
static size_t g_chatShown = 0;
static std::string g_lastNationKey;
static KV g_cfg;
static bool g_inRefresh = false;

static std::wstring CfgPath() { return ModDataDir() + L"config.ini"; }

static void LoadCfg() {
    std::string s;
    if (ReadFileBytes(CfgPath(), s)) g_cfg = KVDecode(s);
    if (!g_cfg.count("port")) g_cfg["port"] = std::to_string(MP_DEFAULT_PORT);
    if (!g_cfg.count("name")) {
        wchar_t buf[64];
        DWORD n = 64;
        g_cfg["name"] = GetUserNameW(buf, &n) ? W2U(buf) : "Admiral";
    }
}

static void SaveCfg() { WriteFileBytes(CfgPath(), KVEncode(g_cfg)); }

static std::string GetText(int id) {
    HWND h = GetDlgItem(g_wnd, id);
    int n = GetWindowTextLengthW(h);
    std::wstring w(n + 1, L'\0');
    GetWindowTextW(h, &w[0], n + 1);
    w.resize(n);
    return W2U(w);
}

static void SetText(int id, const std::string& s) {
    HWND h = GetDlgItem(g_wnd, id);
    std::wstring w = U2W(s);
    int n = GetWindowTextLengthW(h);
    std::wstring cur(n + 1, L'\0');
    GetWindowTextW(h, &cur[0], n + 1);
    cur.resize(n);
    if (cur != w) SetWindowTextW(h, w.c_str());
}

static HWND Ctl(const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id,
                DWORD ex = 0) {
    HWND c = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, g_wnd, (HMENU)(INT_PTR)id,
                             g_inst, nullptr);
    SendMessageW(c, WM_SETFONT, (WPARAM)g_font, TRUE);
    return c;
}

static std::string AddressesText() {
    std::string s, vpn;
    for (auto& a : net::LocalAddresses()) {
        if (!a.vpn.empty())
            vpn += (vpn.empty() ? "" : ",  ") + a.ip + " (" + a.vpn + ")";
        else
            s += (s.empty() ? "" : ",  ") + a.ip;
    }
    std::string out = "Your VPN address: " + (vpn.empty() ? std::string("none found (start Hamachi/ZeroTier/Radmin)") : vpn);
    out += "\r\nOther addresses: " + (s.empty() ? std::string("-") : s);
    return out;
}

static std::string FmtTime(int sec) {
    if (sec < 0) return "";
    char b[32];
    snprintf(b, sizeof(b), "%d:%02d", sec / 60, sec % 60);
    return b;
}

static LRESULT CALLBACK ChatInProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_KEYDOWN && w == VK_RETURN) {
        SendMessageW(g_wnd, WM_COMMAND, MAKEWPARAM(IDC_SEND, BN_CLICKED), 0);
        return 0;
    }
    if (m == WM_CHAR && (w == '\r' || w == '\n')) return 0;
    return CallWindowProcW(g_chatInOld, h, m, w, l);
}

// Selected row of the diplomacy list (the other player's nation index), or -1.
static int SelectedDiploNation() {
    HWND lv = GetDlgItem(g_wnd, IDC_DIPLO);
    int i = ListView_GetNextItem(lv, -1, LVNI_SELECTED);
    if (i < 0) return -1;
    LVITEMW it = {};
    it.mask = LVIF_PARAM;
    it.iItem = i;
    return ListView_GetItem(lv, &it) ? (int)it.lParam : -1;
}

static void OnDiplomacy(int id) {
    int nation = SelectedDiploNation();
    mp::View v = mp::GetView();
    const mp::DiploRow* row = nullptr;
    for (auto& r : v.diplo)
        if (r.nation == nation) row = &r;
    if (!row) {
        MessageBoxW(g_wnd, L"Choose a nation in the diplomacy list first.", L"Diplomacy", MB_OK);
        return;
    }
    std::wstring who = U2W(row->nationName + (row->player.empty() ? "" : " (" + row->player + ")"));
    std::string action;
    switch (id) {
        case IDC_DWAR:
            if (MessageBoxW(g_wnd, (L"Declare war on " + who + L"?\n\nThe war begins when this month is processed. "
                                    L"Until then you can withdraw the declaration.").c_str(),
                            L"Declare war", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
                return;
            action = "war";
            break;
        case IDC_DALLY:
            action = "ally";
            break;
        case IDC_DPEACE:
            action = "peace";
            break;
        case IDC_DLEAVE:
            if (MessageBoxW(g_wnd, (L"Leave the alliance with " + who + L"?").c_str(), L"Leave alliance",
                            MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
                return;
            action = "leave";
            break;
        default:
            action = "cancel";
            break;
    }
    std::string why = mp::Diplomacy(action, nation);
    if (!why.empty()) MessageBoxW(g_wnd, U2W(why).c_str(), L"Diplomacy", MB_OK | MB_ICONINFORMATION);
}

static void OnCommand(int id) {
    std::string err;
    switch (id) {
        case IDC_HOST: {
            g_cfg["name"] = GetText(IDC_NAME);
            g_cfg["port"] = GetText(IDC_PORT);
            g_cfg["limit"] = GetText(IDC_LIMIT);
            SaveCfg();
            int port = atoi(g_cfg["port"].c_str());
            if (port <= 0 || port > 65534) port = MP_DEFAULT_PORT;
            if (!mp::Host(g_cfg["name"], port, GetText(IDC_PW), atoi(g_cfg["limit"].c_str()), err))
                MessageBoxW(g_wnd, U2W("Cannot host: " + err).c_str(), L"Multiplayer", MB_OK | MB_ICONWARNING);
            break;
        }
        case IDC_JOIN: {
            g_cfg["name"] = GetText(IDC_NAME);
            g_cfg["ip"] = Trim(GetText(IDC_IP));
            g_cfg["port"] = GetText(IDC_PORT);
            SaveCfg();
            if (g_cfg["ip"].empty()) {
                MessageBoxW(g_wnd, L"Enter the host's Hamachi (or LAN) IP address.", L"Multiplayer", MB_OK);
                break;
            }
            int port = atoi(g_cfg["port"].c_str());
            if (port <= 0 || port > 65534) port = MP_DEFAULT_PORT;
            if (!mp::Join(g_cfg["name"], g_cfg["ip"], port, GetText(IDC_PW), err))
                MessageBoxW(g_wnd, U2W("Cannot join: " + err).c_str(), L"Multiplayer", MB_OK | MB_ICONWARNING);
            break;
        }
        case IDC_FIND: {
            int port = atoi(GetText(IDC_PORT).c_str());
            if (port <= 0) port = MP_DEFAULT_PORT;
            SetCursor(LoadCursor(nullptr, IDC_WAIT));
            auto found = net::Discover((uint16_t)port, 1200);
            if (found.empty()) {
                MessageBoxW(g_wnd, L"No games found on the LAN/VPN. Ask the host for their IP address.",
                            L"Multiplayer", MB_OK);
            } else {
                SetText(IDC_IP, found[0].first);
                std::string list;
                for (auto& f : found) list += f.first + "  -  " + f.second + "\n";
                MessageBoxW(g_wnd, U2W("Found:\n" + list + "\nThe first one was filled in; press Join.").c_str(),
                            L"Multiplayer", MB_OK);
            }
            break;
        }
        case IDC_LEAVE:
            mp::Leave();
            break;
        case IDC_CLAIM: {
            HWND cb = GetDlgItem(g_wnd, IDC_NATION);
            int sel = (int)SendMessageW(cb, CB_GETCURSEL, 0, 0);
            mp::View v = mp::GetView();
            int row = sel >= 0 ? (int)SendMessageW(cb, CB_GETITEMDATA, sel, 0) : -1;
            if (row < 0 || row >= (int)v.nations.size()) {
                MessageBoxW(g_wnd, L"Choose a nation in the list first.", L"Multiplayer", MB_OK);
                break;
            }
            mp::ClaimNation(v.nations[row].name);
            break;
        }
        case IDC_RELEASE: {
            HWND cb = GetDlgItem(g_wnd, IDC_NATION);
            int sel = (int)SendMessageW(cb, CB_GETCURSEL, 0, 0);
            mp::View v = mp::GetView();
            int row = sel >= 0 ? (int)SendMessageW(cb, CB_GETITEMDATA, sel, 0) : -1;
            if (row < 0 || row >= (int)v.nations.size()) {
                MessageBoxW(g_wnd, L"Choose the nation in the list first, then press Give to AI.", L"Multiplayer", MB_OK);
                break;
            }
            const auto& n = v.nations[row];
            if (!n.claimedBy.empty()) {
                std::wstring q = L"Give " + U2W(n.name) + L" (" + U2W(n.claimedBy) + L") to the AI?";
                if (MessageBoxW(g_wnd, q.c_str(), L"Multiplayer", MB_YESNO | MB_ICONQUESTION) != IDYES) break;
            }
            std::string why = mp::ReleaseNationByName(n.name);
            if (!why.empty()) MessageBoxW(g_wnd, U2W(why).c_str(), L"Multiplayer", MB_OK);
            break;
        }
        case IDC_SETLIMIT:
            g_cfg["limit"] = GetText(IDC_LIMIT);
            SaveCfg();
            mp::SetTurnLimit(atoi(g_cfg["limit"].c_str()));
            break;
        case IDC_START: {
            int r = mp::StartSession();
            if (r == 1)
                MessageBoxW(g_wnd,
                            L"No campaign is open yet.\n\n"
                            L"1. On the title screen choose New Game (pick the nation you want to play) or Load Game.\n"
                            L"2. On a new campaign press Start, then Continue, until the button says Turn.\n\n"
                            L"The session will then start automatically.",
                            L"Start session", MB_OK | MB_ICONINFORMATION);
            else if (r == 2)
                MessageBoxW(g_wnd,
                            L"Your new campaign is still in its setup steps.\n\n"
                            L"Press Start, then Continue, on the campaign screen until the button says Turn.\n\n"
                            L"The session will then start automatically.",
                            L"Start session", MB_OK | MB_ICONINFORMATION);
            break;
        }
        case IDC_ADVANCE:
            if (MessageBoxW(g_wnd, L"Process the turn now, even if not everyone has submitted?", L"Multiplayer",
                            MB_YESNO | MB_ICONQUESTION) == IDYES)
                mp::AdvanceNow();
            break;
        case IDC_SEND:
            mp::SendChat(GetText(IDC_CHATIN));
            SetText(IDC_CHATIN, "");
            break;
        case IDC_DWAR:
        case IDC_DALLY:
        case IDC_DPEACE:
        case IDC_DLEAVE:
        case IDC_DCANCEL:
            OnDiplomacy(id);
            break;
    }
    Refresh();
}

static LRESULT CALLBACK MpWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_COMMAND:
            if (HIWORD(w) == BN_CLICKED) OnCommand(LOWORD(w));
            return 0;
        case WM_NOTIFY: {
            auto* nm = (NMLISTVIEW*)l;
            if (nm->hdr.idFrom == IDC_DIPLO && nm->hdr.code == LVN_ITEMCHANGED && (nm->uChanged & LVIF_STATE) &&
                (nm->uNewState & LVIS_SELECTED) && !g_inRefresh)
                Refresh();
            return 0;
        }
        case WM_CLOSE:
            ShowWindow(h, SW_HIDE);
            return 0;
        case WM_TIMER:
            Refresh();
            return 0;
        case WM_CTLCOLORSTATIC: {
            HDC dc = (HDC)w;
            SetBkMode(dc, TRANSPARENT);
            return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
        }
    }
    return DefWindowProcW(h, m, w, l);
}

static HWND DesiredOwner() {
    void* f = game::BuildCamp();
    if (f && game::FormVisible(f)) return game::FormHwnd(f);
    void* s = game::StartForm();
    if (s && game::FormVisible(s)) return game::FormHwnd(s);
    return nullptr;
}

static void CreateMpWindow() {
    WNDCLASSW wc = {};
    wc.lpfnWndProc = MpWndProc;
    wc.hInstance = g_inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    wc.lpszClassName = L"RTW3MP_Window";
    wc.hIcon = LoadIcon(GetModuleHandleW(nullptr), MAKEINTRESOURCE(1));
    RegisterClassW(&wc);
    DWORD style = WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    // Fit small screens: the chat box gives up height first.
    RECT work = {0, 0, 1024, 768};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    RECT probe = {0, 0, 500, 770};
    AdjustWindowRectEx(&probe, style, FALSE, WS_EX_TOOLWINDOW);
    int overflow = (probe.bottom - probe.top) - (work.bottom - work.top);
    int chatH = overflow > 0 ? std::max(40, 110 - overflow) : 110;
    RECT r = {0, 0, 500, 770 - (110 - chatH)};
    AdjustWindowRectEx(&r, style, FALSE, WS_EX_TOOLWINDOW);
    int winY = std::max((int)work.top, std::min(80, (int)work.bottom - (int)(r.bottom - r.top)));
    g_wnd = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"Rule the Waves 3 - Multiplayer", style, 80, winY,
                            r.right - r.left, r.bottom - r.top, DesiredOwner(), nullptr, g_inst, nullptr);
    int y = 10;
    Ctl(L"STATIC", L"Your name:", 0, 10, y + 3, 70, 18, -1);
    Ctl(L"EDIT", U2W(g_cfg["name"]).c_str(), WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 85, y, 150, 22, IDC_NAME);
    Ctl(L"STATIC", L"Password:", 0, 250, y + 3, 60, 18, -1);
    Ctl(L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL | ES_PASSWORD, 315, y, 175, 22, IDC_PW);
    y += 30;
    Ctl(L"STATIC", L"Host IP:", 0, 10, y + 3, 70, 18, -1);
    Ctl(L"EDIT", U2W(g_cfg["ip"]).c_str(), WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 85, y, 150, 22, IDC_IP);
    Ctl(L"STATIC", L"Port:", 0, 250, y + 3, 60, 18, -1);
    Ctl(L"EDIT", U2W(g_cfg["port"]).c_str(), WS_BORDER | WS_TABSTOP | ES_NUMBER, 315, y, 60, 22, IDC_PORT);
    y += 32;
    Ctl(L"BUTTON", L"Host game", WS_TABSTOP, 10, y, 110, 28, IDC_HOST);
    Ctl(L"BUTTON", L"Join game", WS_TABSTOP, 130, y, 110, 28, IDC_JOIN);
    Ctl(L"BUTTON", L"Find LAN games", WS_TABSTOP, 250, y, 120, 28, IDC_FIND);
    Ctl(L"BUTTON", L"Leave", WS_TABSTOP, 380, y, 110, 28, IDC_LEAVE);
    y += 36;
    Ctl(L"STATIC", U2W(AddressesText()).c_str(), SS_LEFT, 10, y, 480, 34, IDC_ADDRS);
    y += 40;
    HWND st = Ctl(L"STATIC", L"Not connected.", SS_LEFT | SS_SUNKEN, 10, y, 480, 92, IDC_STATUS);
    SendMessageW(st, WM_SETFONT, (WPARAM)g_bold, TRUE);
    y += 100;
    HWND lv = Ctl(WC_LISTVIEWW, L"", LVS_REPORT | LVS_SINGLESEL | LVS_NOSORTHEADER | WS_BORDER, 10, y, 480, 104,
                  IDC_PLAYERS);
    ListView_SetExtendedListViewStyle(lv, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    const wchar_t* cols[] = {L"Player", L"Nation", L"Status", L"Ping"};
    int widths[] = {140, 140, 130, 60};
    for (int i = 0; i < 4; i++) {
        LVCOLUMNW c = {};
        c.mask = LVCF_TEXT | LVCF_WIDTH;
        c.pszText = (LPWSTR)cols[i];
        c.cx = widths[i];
        ListView_InsertColumn(lv, i, &c);
    }
    y += 112;
    Ctl(L"STATIC", L"Nation:", 0, 10, y + 4, 50, 18, -1);
    Ctl(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 65, y, 230, 300, IDC_NATION);
    Ctl(L"BUTTON", L"Claim nation", WS_TABSTOP, 305, y - 1, 90, 26, IDC_CLAIM);
    Ctl(L"BUTTON", L"Give to AI", WS_TABSTOP, 400, y - 1, 90, 26, IDC_RELEASE);
    y += 34;
    Ctl(L"STATIC", L"Turn time limit (minutes, 0 = none):", 0, 10, y + 4, 210, 18, IDC_LIMITLBL);
    Ctl(L"EDIT", U2W(g_cfg.count("limit") ? g_cfg["limit"] : "0").c_str(), WS_BORDER | WS_TABSTOP | ES_NUMBER,
        225, y, 45, 22, IDC_LIMIT);
    Ctl(L"BUTTON", L"Set", WS_TABSTOP, 275, y - 1, 50, 26, IDC_SETLIMIT);
    y += 32;
    Ctl(L"BUTTON", L"Start session", WS_TABSTOP, 10, y, 150, 30, IDC_START);
    Ctl(L"BUTTON", L"Advance turn now", WS_TABSTOP, 170, y, 150, 30, IDC_ADVANCE);
    y += 40;
    Ctl(L"STATIC", L"Diplomacy with the other players", SS_LEFT | SS_ENDELLIPSIS, 10, y, 480, 18, IDC_DIPLOLBL);
    y += 20;
    HWND dlv = Ctl(WC_LISTVIEWW, L"", LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOSORTHEADER | WS_BORDER |
                   WS_TABSTOP, 10, y, 480, 96, IDC_DIPLO);
    ListView_SetExtendedListViewStyle(dlv, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    const wchar_t* dcols[] = {L"Nation", L"Player", L"Relation", L"Tension", L"Pending"};
    int dwidths[] = {112, 100, 62, 52, 150};
    for (int i = 0; i < 5; i++) {
        LVCOLUMNW c = {};
        c.mask = LVCF_TEXT | LVCF_WIDTH;
        c.pszText = (LPWSTR)dcols[i];
        c.cx = dwidths[i];
        ListView_InsertColumn(dlv, i, &c);
    }
    y += 100;
    Ctl(L"BUTTON", L"Declare war", WS_TABSTOP, 10, y, 88, 28, IDC_DWAR);
    Ctl(L"BUTTON", L"Propose alliance", WS_TABSTOP, 103, y, 104, 28, IDC_DALLY);
    Ctl(L"BUTTON", L"Propose peace", WS_TABSTOP, 212, y, 96, 28, IDC_DPEACE);
    Ctl(L"BUTTON", L"Leave alliance", WS_TABSTOP, 313, y, 92, 28, IDC_DLEAVE);
    Ctl(L"BUTTON", L"Withdraw", WS_TABSTOP, 410, y, 80, 28, IDC_DCANCEL);
    y += 36;
    Ctl(L"EDIT", L"", WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, 10, y, 480, chatH,
        IDC_CHATLOG);
    y += chatH + 6;
    HWND in = Ctl(L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 10, y, 400, 24, IDC_CHATIN);
    g_chatInOld = (WNDPROC)SetWindowLongPtrW(in, GWLP_WNDPROC, (LONG_PTR)ChatInProc);
    Ctl(L"BUTTON", L"Send", WS_TABSTOP, 415, y - 1, 75, 26, IDC_SEND);
    SetTimer(g_wnd, 1, 1000, nullptr);
}

static void RefreshInner();

void Refresh() {
    if (!g_wnd || g_inRefresh) return;
    g_inRefresh = true;
    try {
        RefreshInner();
    } catch (...) {
        Log("exception in the Multiplayer window refresh");
    }
    g_inRefresh = false;
}

static void RefreshDiplomacy(const mp::View& v) {
    SetText(IDC_DIPLOLBL, "Diplomacy" + (v.diploNote.empty() ? std::string(" with the other players") : ": " + v.diploNote));
    HWND lv = GetDlgItem(g_wnd, IDC_DIPLO);
    int sel = SelectedDiploNation();
    bool selFound = false;
    for (auto& r : v.diplo) selFound |= r.nation == sel;
    if (!selFound) sel = -1;  // fall back to the first row
    bool rebuild = ListView_GetItemCount(lv) != (int)v.diplo.size();
    for (size_t i = 0; !rebuild && i < v.diplo.size(); i++) {
        LVITEMW it = {};
        it.mask = LVIF_PARAM;
        it.iItem = (int)i;
        rebuild = !ListView_GetItem(lv, &it) || (int)it.lParam != v.diplo[i].nation;
    }
    if (rebuild) {
        ListView_DeleteAllItems(lv);
        for (size_t i = 0; i < v.diplo.size(); i++) {
            LVITEMW it = {};
            it.mask = LVIF_TEXT | LVIF_PARAM;
            it.iItem = (int)i;
            it.pszText = (LPWSTR)L"";
            it.lParam = v.diplo[i].nation;
            ListView_InsertItem(lv, &it);
        }
    }
    const mp::DiploRow* cur = nullptr;
    for (size_t i = 0; i < v.diplo.size(); i++) {
        const auto& r = v.diplo[i];
        std::wstring cols[5] = {U2W(r.nationName), U2W(r.player), U2W(r.relation),
                                r.atWar ? L"war" : std::to_wstring(r.tension), U2W(r.pending)};
        for (int c = 0; c < 5; c++) {
            wchar_t buf[256] = {};
            ListView_GetItemText(lv, (int)i, c, buf, 255);
            if (cols[c] != buf) ListView_SetItemText(lv, (int)i, c, (LPWSTR)cols[c].c_str());
        }
        bool want = r.nation == sel || (sel < 0 && i == 0);  // keep the selection; preselect the first row
        if (want) cur = &r;
        if (want != ((ListView_GetItemState(lv, (int)i, LVIS_SELECTED) & LVIS_SELECTED) != 0))
            ListView_SetItemState(lv, (int)i, want ? LVIS_SELECTED : 0, LVIS_SELECTED);
    }
    bool open = v.diploOpen && cur;
    SetText(IDC_DALLY, cur && cur->allyAccept ? "Accept alliance" : "Propose alliance");
    SetText(IDC_DPEACE, cur && cur->peaceAccept ? "Accept peace" : "Propose peace");
    SetText(IDC_DCANCEL, cur && cur->cancelIsDecline ? "Decline" : "Withdraw");
    EnableWindow(GetDlgItem(g_wnd, IDC_DWAR), open && cur->canWar);
    EnableWindow(GetDlgItem(g_wnd, IDC_DALLY), open && (cur->allyAccept || cur->canAlly));
    EnableWindow(GetDlgItem(g_wnd, IDC_DPEACE), open && (cur->peaceAccept || cur->canPeace));
    EnableWindow(GetDlgItem(g_wnd, IDC_DLEAVE), open && cur->canLeave);
    EnableWindow(GetDlgItem(g_wnd, IDC_DCANCEL), open && cur->canCancel);
}

static void RefreshInner() {
    mp::View v = mp::GetView();
    bool none = v.role == mp::Role::None, host = v.role == mp::Role::Host, client = v.role == mp::Role::Client;
    EnableWindow(GetDlgItem(g_wnd, IDC_HOST), none);
    EnableWindow(GetDlgItem(g_wnd, IDC_JOIN), none);
    EnableWindow(GetDlgItem(g_wnd, IDC_FIND), none);
    EnableWindow(GetDlgItem(g_wnd, IDC_LEAVE), !none);
    EnableWindow(GetDlgItem(g_wnd, IDC_NAME), none);
    EnableWindow(GetDlgItem(g_wnd, IDC_IP), none);
    EnableWindow(GetDlgItem(g_wnd, IDC_PORT), none);
    EnableWindow(GetDlgItem(g_wnd, IDC_PW), none);
    EnableWindow(GetDlgItem(g_wnd, IDC_NATION), !none);
    EnableWindow(GetDlgItem(g_wnd, IDC_CLAIM),
                 !v.nations.empty() && (client || (host && !v.campaignLoaded && v.phase == mp::Phase::Lobby)));
    EnableWindow(GetDlgItem(g_wnd, IDC_RELEASE), host);
    EnableWindow(GetDlgItem(g_wnd, IDC_SETLIMIT), host);
    EnableWindow(GetDlgItem(g_wnd, IDC_LIMIT), host || none);
    EnableWindow(GetDlgItem(g_wnd, IDC_START), host && v.phase == mp::Phase::Lobby);
    EnableWindow(GetDlgItem(g_wnd, IDC_ADVANCE), host && v.phase == mp::Phase::Planning);
    for (int id : {IDC_START, IDC_ADVANCE, IDC_RELEASE, IDC_SETLIMIT, IDC_LIMIT, IDC_LIMITLBL})  // host-only controls
        ShowWindow(GetDlgItem(g_wnd, id), client ? SW_HIDE : SW_SHOWNA);
    EnableWindow(GetDlgItem(g_wnd, IDC_SEND), !none);

    std::string role = none ? "Not connected" : host ? "Hosting" : "Joined " + v.hostName + "'s game";
    if (!none && !v.myName.empty()) SetText(IDC_NAME, v.myName);  // the host may have renamed a duplicate
    std::string phase = v.phase == mp::Phase::Lobby ? "lobby" : v.phase == mp::Phase::Planning ? "planning" : "turn being processed";
    std::string line1 = role;
    if (!none) {
        line1 += "  |  " + phase;
        if (!v.date.empty()) line1 += "  |  " + v.date;
        if (v.remainingSec >= 0 && v.phase == mp::Phase::Planning) line1 += "  |  time left " + FmtTime(v.remainingSec);
    }
    std::string line2 = v.myNationName.empty() ? "" : "You command: " + v.myNationName + (client && v.submitted ? " (turn submitted)" : "");
    std::string line3 = v.hint.empty() ? v.status : v.hint;
    SetText(IDC_STATUS, line1 + "\r\n" + line2 + "\r\n" + line3);

    HWND lv = GetDlgItem(g_wnd, IDC_PLAYERS);
    int count = ListView_GetItemCount(lv);
    if (count != (int)v.players.size()) {
        ListView_DeleteAllItems(lv);
        for (size_t i = 0; i < v.players.size(); i++) {
            LVITEMW it = {};
            it.mask = LVIF_TEXT;
            it.iItem = (int)i;
            it.pszText = (LPWSTR)L"";
            ListView_InsertItem(lv, &it);
        }
    }
    for (size_t i = 0; i < v.players.size(); i++) {
        auto& p = v.players[i];
        std::wstring cols[4] = {
            U2W(p.name + (p.peer == 0 ? " (host)" : "")), U2W(p.nationName.empty() ? "-" : p.nationName),
            p.connected ? (v.phase == mp::Phase::Planning ? (p.submitted ? L"ready" : L"planning") : L"connected")
                        : L"disconnected",
            p.ping >= 0 ? std::to_wstring(p.ping) + L" ms" : L""};
        for (int c = 0; c < 4; c++) {
            wchar_t buf[128] = {};
            ListView_GetItemText(lv, (int)i, c, buf, 127);
            if (cols[c] != buf) ListView_SetItemText(lv, (int)i, c, (LPWSTR)cols[c].c_str());
        }
    }

    std::string key;
    for (auto& n : v.nations) key += std::to_string(n.index) + n.name + ":" + n.claimedBy + ";";
    if (key != g_lastNationKey) {
        g_lastNationKey = key;
        HWND cb = GetDlgItem(g_wnd, IDC_NATION);
        int sel = (int)SendMessageW(cb, CB_GETCURSEL, 0, 0);
        std::wstring selName;
        if (sel >= 0) {
            int len = (int)SendMessageW(cb, CB_GETLBTEXTLEN, sel, 0);
            std::wstring t(len + 1, L'\0');
            SendMessageW(cb, CB_GETLBTEXT, sel, (LPARAM)&t[0]);
            selName = t.substr(0, t.find(L"  ("));
            selName = selName.c_str();
        }
        SendMessageW(cb, CB_RESETCONTENT, 0, 0);
        for (size_t r = 0; r < v.nations.size(); r++) {
            const auto& n = v.nations[r];
            std::wstring t = U2W(n.name);
            if (!n.claimedBy.empty()) t += L"  (" + U2W(n.claimedBy) + L")";
            int i = (int)SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)t.c_str());
            SendMessageW(cb, CB_SETITEMDATA, i, (LPARAM)r);
            if (!selName.empty() && U2W(n.name) == selName) SendMessageW(cb, CB_SETCURSEL, i, 0);
        }
    }

    RefreshDiplomacy(v);

    const auto& chat = mp::Chat();
    if (chat.size() != g_chatShown) {
        g_chatShown = chat.size();
        std::string all;
        for (auto& c : chat) all += c + "\r\n";
        HWND log = GetDlgItem(g_wnd, IDC_CHATLOG);
        SetWindowTextW(log, U2W(all).c_str());
        SendMessageW(log, EM_LINESCROLL, 0, 100000);
    }
}

void ShowMpWindow() {
    if (!g_wnd) CreateMpWindow();
    HWND owner = DesiredOwner();
    if (owner && (HWND)GetWindowLongPtrW(g_wnd, GWLP_HWNDPARENT) != owner) SetWindowLongPtrW(g_wnd, GWLP_HWNDPARENT, (LONG_PTR)owner);
    SetText(IDC_ADDRS, AddressesText());
    Refresh();
    ShowWindow(g_wnd, SW_SHOWNORMAL);
    SetForegroundWindow(g_wnd);
}

void ToggleMpWindow() {
    if (g_wnd && IsWindowVisible(g_wnd))
        ShowWindow(g_wnd, SW_HIDE);
    else
        ShowMpWindow();
}

void Notice(const std::string& text) {
    if (!g_wnd) return;
    if (!IsWindowVisible(g_wnd) && mp::GetView().role != mp::Role::None) ShowWindow(g_wnd, SW_SHOWNOACTIVATE);
    FLASHWINFO fi = {sizeof(fi), g_wnd, FLASHW_CAPTION, 3, 0};
    FlashWindowEx(&fi);
    Refresh();
}

bool PreTranslate(MSG* msg) {
    // Only Tab navigation goes through the dialog manager. Mouse messages must never be touched here:
    // this runs inside a GetMessage hook, and Windows' own tracking loops (title-bar buttons, scroll bars,
    // drop-downs) wait for the mouse-up message themselves.
    if (!g_wnd || !IsWindowVisible(g_wnd)) return false;
    if ((msg->message != WM_KEYDOWN && msg->message != WM_KEYUP && msg->message != WM_CHAR) || msg->wParam != VK_TAB)
        return false;
    if (msg->hwnd != g_wnd && !IsChild(g_wnd, msg->hwnd)) return false;
    return IsDialogMessageW(g_wnd, msg) != 0;
}

// ---------------------------------------------------------------------------------------------
// "Multiplayer" entry on the title screen: a self-contained child control (paints and handles its own
// clicks), so the game's VCL-styled panel is never subclassed.

static bool g_menuPressed = false, g_menuHover = false;

static LRESULT CALLBACK MenuButtonProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(h, &ps);
            RECT r;
            GetClientRect(h, &r);
            HBRUSH bg = CreateSolidBrush(g_menuPressed ? RGB(196, 190, 172) : g_menuHover ? RGB(245, 242, 230) : RGB(232, 228, 214));
            FillRect(dc, &r, bg);
            DeleteObject(bg);
            HPEN pen = CreatePen(PS_SOLID, 3, RGB(0, 0, 0));
            HGDIOBJ op = SelectObject(dc, pen);
            HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
            Rectangle(dc, r.left + 1, r.top + 1, r.right - 1, r.bottom - 1);
            SelectObject(dc, ob);
            SelectObject(dc, op);
            DeleteObject(pen);
            HGDIOBJ of = SelectObject(dc, g_menuFont);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(0, 0, 0));
            DrawTextW(dc, L"Multiplayer", -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(dc, of);
            EndPaint(h, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_SETCURSOR:
            SetCursor(LoadCursor(nullptr, IDC_HAND));
            return TRUE;
        case WM_MOUSEMOVE:
            if (!g_menuHover) {
                g_menuHover = true;
                TRACKMOUSEEVENT t = {sizeof(t), TME_LEAVE, h, 0};
                TrackMouseEvent(&t);
                InvalidateRect(h, nullptr, FALSE);
            }
            return 0;
        case WM_MOUSELEAVE:
            g_menuHover = false;
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        case WM_LBUTTONDOWN:
            g_menuPressed = true;
            SetCapture(h);
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        case WM_LBUTTONUP: {
            bool was = g_menuPressed;
            g_menuPressed = false;
            ReleaseCapture();
            InvalidateRect(h, nullptr, FALSE);
            POINT pt = {(short)LOWORD(l), (short)HIWORD(l)};
            RECT r;
            GetClientRect(h, &r);
            if (was && PtInRect(&r, pt)) ShowMpWindow();
            return 0;
        }
    }
    return DefWindowProcW(h, m, w, l);
}

static void EnsureMenuButton() {
    void* start = game::StartForm();
    if (!start) return;
    static int fStartPanel = -2;
    if (fStartPanel == -2) fStartPanel = dl::Field("TfrmSelectNation2", "pnStart");
    if (fStartPanel < 0) return;
    void* panel = dl::At<void*>(start, fStartPanel);
    HWND ph = game::FormHwnd(panel);
    if (!ph || !IsWindow(ph)) return;
    if (g_menuBtn && IsWindow(g_menuBtn) && GetParent(g_menuBtn) == ph) return;
    if (g_menuBtn && IsWindow(g_menuBtn)) DestroyWindow(g_menuBtn);
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = MenuButtonProc;
        wc.hInstance = g_inst;
        wc.hCursor = LoadCursor(nullptr, IDC_HAND);
        wc.lpszClassName = L"RTW3MP_MenuButton";
        RegisterClassW(&wc);
        registered = true;
    }
    // Above the "Exit" box of the title image (panel client coordinates, 1001x512 design size).
    RECT pr;
    GetClientRect(ph, &pr);
    double sx = pr.right / 1001.0, sy = pr.bottom / 512.0;
    int x = (int)(808 * sx), y = (int)(400 * sy), w = (int)(177 * sx), hgt = (int)(40 * sy);
    g_menuBtn = CreateWindowExW(0, L"RTW3MP_MenuButton", L"Multiplayer", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, x, y,
                                w, hgt, ph, (HMENU)(INT_PTR)IDC_STARTMENU, g_inst, nullptr);
    SetWindowPos(g_menuBtn, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    Log("title screen: Multiplayer button added");
}

void Init(HINSTANCE inst) {
    g_inst = inst;
    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);
    g_font = CreateFontW(-13, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_bold = CreateFontW(-13, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_menuFont = CreateFontW(-30, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0,
                             L"Times New Roman");
    LoadCfg();
    mp::g_onChanged = [] { Refresh(); };
    mp::g_onNotice = [](const std::string& s) { Notice(s); };
}

void Tick() {
    EnsureMenuButton();
    if (g_wnd && IsWindowVisible(g_wnd)) {
        HWND owner = DesiredOwner();
        if (owner && (HWND)GetWindowLongPtrW(g_wnd, GWLP_HWNDPARENT) != owner)
            SetWindowLongPtrW(g_wnd, GWLP_HWNDPARENT, (LONG_PTR)owner);
    }
}

}  // namespace ui
