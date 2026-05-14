#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>

#include "Protocol.h"
#include "SecureChannel.h"

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/event.h>
#include <wx/frame.h>
#include <wx/grid.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/notebook.h>
#include <wx/panel.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/wx.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#pragma comment(lib, "Ws2_32.lib")

wxDECLARE_EVENT(EVT_COMMAND_FINISHED, wxThreadEvent);
wxDEFINE_EVENT(EVT_COMMAND_FINISHED, wxThreadEvent);

namespace {

struct ServerResponse {
    bool ok = false;
    bool connected = false;
    std::string firstLine;
    std::vector<std::string> bodyLines;
    std::string error;

    std::string rawText() const {
        std::ostringstream output;
        if (!firstLine.empty()) {
            output << firstLine << '\n';
        }
        for (const std::string& line : bodyLines) {
            output << line << '\n';
        }
        return output.str();
    }
};

struct CourseRow {
    std::string code;
    std::string title;
    std::string section;
    std::string instructor;
    std::string day;
    std::string start;
    std::string end;
    std::string classroom;
    std::string semester;
};

class CourseGridTable final : public wxGridTableBase {
public:
    int GetNumberRows() override {
        return static_cast<int>(rows_.size());
    }

    int GetNumberCols() override {
        return 9;
    }

    bool IsEmptyCell(int row, int col) override {
        return GetValue(row, col).empty();
    }

    wxString GetValue(int row, int col) override {
        if (row < 0 || col < 0 ||
            row >= static_cast<int>(rows_.size()) || col >= GetNumberCols()) {
            return "";
        }

        const CourseRow& course = rows_[static_cast<std::size_t>(row)];
        switch (col) {
            case 0:
                return wxString::FromUTF8(course.code.c_str());
            case 1:
                return wxString::FromUTF8(course.title.c_str());
            case 2:
                return wxString::FromUTF8(course.section.c_str());
            case 3:
                return wxString::FromUTF8(course.instructor.c_str());
            case 4:
                return wxString::FromUTF8(course.day.c_str());
            case 5:
                return wxString::FromUTF8(course.start.c_str());
            case 6:
                return wxString::FromUTF8(course.end.c_str());
            case 7:
                return wxString::FromUTF8(course.classroom.c_str());
            case 8:
                return wxString::FromUTF8(course.semester.c_str());
            default:
                return "";
        }
    }

    void SetValue(int, int, const wxString&) override {
    }

    wxString GetColLabelValue(int col) override {
        static const char* labels[] = {
            "Code", "Title", "Section", "Instructor", "Day",
            "Start", "End", "Classroom", "Semester"
        };
        if (col < 0 || col >= GetNumberCols()) {
            return "";
        }
        return labels[col];
    }

    void SetRows(std::vector<CourseRow> rows) {
        rows_ = std::move(rows);
    }

private:
    std::vector<CourseRow> rows_;
};

const wxColour kCanvas(255, 255, 255);
const wxColour kSurfaceSoft(244, 244, 244);
const wxColour kSurfaceCard(255, 255, 255);
const wxColour kSurfaceDark(23, 26, 32);
const wxColour kSurfaceDarkElevated(57, 60, 65);
const wxColour kPrimary(62, 106, 225);
const wxColour kPrimaryActive(47, 84, 189);
const wxColour kInk(23, 26, 32);
const wxColour kBody(57, 60, 65);
const wxColour kMuted(92, 94, 98);
const wxColour kHairline(238, 238, 238);
const wxColour kOnDark(255, 255, 255);
const wxColour kOnDarkSoft(208, 209, 210);
const wxColour kSuccess(32, 126, 79);
const wxColour kError(182, 43, 43);

std::string wxToStd(const wxString& value) {
    return std::string(value.mb_str());
}

wxString stdToWx(const std::string& value) {
    return wxString::FromUTF8(value.c_str());
}

wxFont bodyFont(int pointSize = 10, wxFontWeight weight = wxFONTWEIGHT_NORMAL) {
    return wxFont(pointSize,
                  wxFONTFAMILY_SWISS,
                  wxFONTSTYLE_NORMAL,
                  weight,
                  false,
                  "Segoe UI");
}

wxFont displayFont(int pointSize = 18, wxFontWeight weight = wxFONTWEIGHT_MEDIUM) {
    return wxFont(pointSize,
                  wxFONTFAMILY_SWISS,
                  wxFONTSTYLE_NORMAL,
                  weight,
                  false,
                  "Segoe UI");
}

void stylePanel(wxWindow* window, const wxColour& background = kCanvas) {
    if (window == nullptr) {
        return;
    }
    window->SetBackgroundColour(background);
    window->SetForegroundColour(kInk);
    window->SetFont(bodyFont());
    window->SetDoubleBuffered(true);
}

void styleTextCtrl(wxTextCtrl* control, bool dark = false) {
    if (control == nullptr) {
        return;
    }
    control->SetBackgroundColour(dark ? kSurfaceDarkElevated : kSurfaceSoft);
    control->SetForegroundColour(dark ? kOnDark : kInk);
    control->SetFont(dark ? wxFont(9,
                                   wxFONTFAMILY_TELETYPE,
                                   wxFONTSTYLE_NORMAL,
                                   wxFONTWEIGHT_NORMAL,
                                   false,
                                   "Consolas")
                          : bodyFont());
}

void styleChoice(wxChoice* choice) {
    if (choice == nullptr) {
        return;
    }
    choice->SetBackgroundColour(kSurfaceSoft);
    choice->SetForegroundColour(kInk);
    choice->SetFont(bodyFont());
}

void styleButton(wxButton* button, bool primary = false, bool darkSurface = false) {
    if (button == nullptr) {
        return;
    }
    button->SetFont(bodyFont(10, wxFONTWEIGHT_MEDIUM));
    button->SetMinSize(wxSize(primary ? 124 : 96, 34));
    if (primary) {
        button->SetBackgroundColour(kPrimary);
        button->SetOwnBackgroundColour(kPrimary);
        button->SetForegroundColour(kOnDark);
        button->SetOwnForegroundColour(kOnDark);
    } else if (darkSurface) {
        button->SetBackgroundColour(kSurfaceDarkElevated);
        button->SetForegroundColour(kOnDark);
    } else {
        button->SetBackgroundColour(kSurfaceSoft);
        button->SetForegroundColour(kInk);
    }
}

void styleStaticBox(wxStaticBoxSizer* sizer, bool dark = false) {
    if (sizer == nullptr || sizer->GetStaticBox() == nullptr) {
        return;
    }
    sizer->GetStaticBox()->SetBackgroundColour(dark ? kSurfaceDark : kCanvas);
    sizer->GetStaticBox()->SetForegroundColour(dark ? kOnDark : kBody);
    sizer->GetStaticBox()->SetFont(bodyFont(10, wxFONTWEIGHT_MEDIUM));
}

bool parseCoursePayloadRow(const std::string& line, CourseRow& row) {
    const std::vector<std::string> fields = Protocol::splitByChar(line, '|');
    if (fields.size() != 9) {
        return false;
    }

    row.code = fields[0];
    row.title = fields[1];
    row.section = fields[2];
    row.instructor = fields[3];
    row.day = fields[4];
    row.start = fields[5];
    row.end = fields[6];
    row.classroom = fields[7];
    row.semester = fields[8];
    return true;
}

bool isTimeToken(const std::string& value) {
    return value.size() == 5 &&
           std::isdigit(static_cast<unsigned char>(value[0])) &&
           std::isdigit(static_cast<unsigned char>(value[1])) &&
           value[2] == ':' &&
           std::isdigit(static_cast<unsigned char>(value[3])) &&
           std::isdigit(static_cast<unsigned char>(value[4]));
}

bool isDayToken(const std::string& value) {
    const std::string day = Protocol::toUpper(Protocol::trim(value));
    return day == "MON" || day == "TUE" || day == "WED" || day == "THU" ||
           day == "FRI" || day == "SAT" || day == "SUN";
}

std::string joinTokens(const std::vector<std::string>& tokens,
                       std::size_t first,
                       std::size_t lastExclusive) {
    std::ostringstream output;
    for (std::size_t index = first; index < lastExclusive; ++index) {
        if (index > first) {
            output << ' ';
        }
        output << tokens[index];
    }
    return output.str();
}

bool parseWhitespaceCourseRow(const std::string& line, CourseRow& row) {
    const std::vector<std::string> tokens = Protocol::splitWhitespace(line);
    if (tokens.size() < 9) {
        return false;
    }

    std::size_t dayIndex = tokens.size();
    for (std::size_t index = 2; index < tokens.size(); ++index) {
        if (isDayToken(tokens[index]) &&
            index + 4 < tokens.size() &&
            isTimeToken(tokens[index + 1]) &&
            isTimeToken(tokens[index + 2])) {
            dayIndex = index;
            break;
        }
    }
    if (dayIndex == tokens.size()) {
        return false;
    }

    std::size_t sectionIndex = tokens.size();
    for (std::size_t index = 1; index < dayIndex; ++index) {
        if (tokens[index].size() == 1) {
            sectionIndex = index;
            break;
        }
    }
    if (sectionIndex == tokens.size()) {
        for (std::size_t index = 2; index < dayIndex; ++index) {
            if (tokens[index].size() <= 3) {
                sectionIndex = index;
                break;
            }
        }
    }
    if (sectionIndex == tokens.size() ||
        sectionIndex == 1 ||
        sectionIndex + 1 >= dayIndex) {
        return false;
    }

    row.code = tokens[0];
    row.title = joinTokens(tokens, 1, sectionIndex);
    row.section = tokens[sectionIndex];
    row.instructor = joinTokens(tokens, sectionIndex + 1, dayIndex);
    row.day = tokens[dayIndex];
    row.start = tokens[dayIndex + 1];
    row.end = tokens[dayIndex + 2];
    row.classroom = tokens[dayIndex + 3];
    row.semester = tokens[dayIndex + 4];
    return !row.title.empty() && !row.instructor.empty();
}

std::vector<CourseRow> parseCourseRows(const ServerResponse& response) {
    std::vector<CourseRow> rows;
    if (!Protocol::startsWithIgnoreCase(response.firstLine, "RESULT ")) {
        return rows;
    }

    for (const std::string& line : response.bodyLines) {
        const std::string trimmed = Protocol::trim(line);
        if (trimmed.empty() ||
            Protocol::startsWithIgnoreCase(trimmed, "END") ||
            Protocol::startsWithIgnoreCase(trimmed, "No matching courses.")) {
            continue;
        }

        CourseRow row;
        if (parseCoursePayloadRow(trimmed, row) || parseWhitespaceCourseRow(trimmed, row)) {
            rows.push_back(row);
        }
    }

    return rows;
}

void parseSecureServerBlock(const std::string& block, ServerResponse& response) {
    std::istringstream input(block);
    std::getline(input, response.firstLine);
    if (!response.firstLine.empty() && response.firstLine.back() == '\r') {
        response.firstLine.pop_back();
    }

    if (Protocol::startsWithIgnoreCase(response.firstLine, "RESULT ")) {
        std::string line;
        while (std::getline(input, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (line == "END") {
                break;
            }
            response.bodyLines.push_back(line);
        }
    }
}

class CourseTcpClient {
public:
    ~CourseTcpClient() {
        disconnect();
    }

    bool isConnected() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return socket_ != INVALID_SOCKET;
    }

    bool isSecure() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return secure_;
    }

    ServerResponse connectTo(const std::string& host, int port, const std::string& securePsk) {
        std::lock_guard<std::mutex> lock(mutex_);
        ServerResponse response;

        closeSocketUnlocked();

        SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (serverSocket == INVALID_SOCKET) {
            response.error = "socket failed: " + std::to_string(WSAGetLastError());
            return response;
        }

        sockaddr_in serverAddress = {};
        serverAddress.sin_family = AF_INET;
        serverAddress.sin_port = htons(static_cast<u_short>(port));
        serverAddress.sin_addr.s_addr = inet_addr(host.c_str());

        if (serverAddress.sin_addr.s_addr == INADDR_NONE && host != "255.255.255.255") {
            closesocket(serverSocket);
            response.error = "Invalid IPv4 address: " + host;
            return response;
        }

        if (connect(serverSocket,
                    reinterpret_cast<sockaddr*>(&serverAddress),
                    sizeof(serverAddress)) == SOCKET_ERROR) {
            const int errorCode = WSAGetLastError();
            closesocket(serverSocket);
            response.error = "connect failed: " + std::to_string(errorCode);
            return response;
        }

        std::string greeting;
        if (!Protocol::recvLine(serverSocket, greeting)) {
            closesocket(serverSocket);
            response.error = "Connected, but the server did not send a greeting.";
            return response;
        }

        socket_ = serverSocket;
        response.firstLine = greeting;

        if (!securePsk.empty()) {
            std::string handshakeError;
            if (!SecureChannel::clientHandshake(serverSocket, securePsk, aesKey_, handshakeError)) {
                closesocket(serverSocket);
                socket_ = INVALID_SOCKET;
                aesKey_.clear();
                secure_ = false;
                response.error = handshakeError;
                response.connected = false;
                response.ok = false;
                return response;
            }
            secure_ = true;
        }

        response.ok = true;
        response.connected = true;
        return response;
    }

    ServerResponse sendCommand(const std::string& command) {
        std::lock_guard<std::mutex> lock(mutex_);
        ServerResponse response;
        response.connected = socket_ != INVALID_SOCKET;

        if (socket_ == INVALID_SOCKET) {
            response.error = "Not connected to the server.";
            return response;
        }

        if (secure_) {
            if (!SecureChannel::sendSecureFrame(socket_, aesKey_, command + "\n")) {
                closeSocketUnlocked();
                response.connected = false;
                response.error = "Failed to send request to the server.";
                return response;
            }

            std::string block;
            if (!SecureChannel::recvSecureFrame(socket_, aesKey_, block)) {
                closeSocketUnlocked();
                response.connected = false;
                response.error = "Server disconnected.";
                return response;
            }

            response.ok = true;
            response.connected = socket_ != INVALID_SOCKET;
            parseSecureServerBlock(block, response);
            return response;
        }

        if (!Protocol::sendLine(socket_, command)) {
            closeSocketUnlocked();
            response.connected = false;
            response.error = "Failed to send request to the server.";
            return response;
        }

        std::string line;
        if (!Protocol::recvLine(socket_, line)) {
            closeSocketUnlocked();
            response.connected = false;
            response.error = "Server disconnected.";
            return response;
        }

        response.ok = true;
        response.connected = socket_ != INVALID_SOCKET;
        response.firstLine = line;

        if (Protocol::startsWithIgnoreCase(line, "RESULT ")) {
            while (Protocol::recvLine(socket_, line)) {
                if (line == "END") {
                    break;
                }
                response.bodyLines.push_back(line);
            }
        }

        return response;
    }

    void disconnect() {
        std::lock_guard<std::mutex> lock(mutex_);
        closeSocketUnlocked();
    }

private:
    void closeSocketUnlocked() {
        if (socket_ != INVALID_SOCKET) {
            closesocket(socket_);
            socket_ = INVALID_SOCKET;
        }
        secure_ = false;
        aesKey_.clear();
    }

    mutable std::mutex mutex_;
    SOCKET socket_ = INVALID_SOCKET;
    bool secure_ = false;
    std::vector<std::uint8_t> aesKey_;
};

class MainFrame : public wxFrame {
public:
    MainFrame()
        : wxFrame(nullptr,
                  wxID_ANY,
                  "Course Timetable Inquiry System",
                  wxDefaultPosition,
                  wxSize(1320, 780)) {
        BuildMenu();
        BuildLayout();
        BindEvents();
        UpdateConnectionState(false);
        Centre();
    }

    ~MainFrame() override {
        closing_ = true;
        client_.disconnect();
        if (worker_.joinable()) {
            worker_.join();
        }
    }

private:
    void BuildMenu() {
        wxMenu* fileMenu = new wxMenu();
        fileMenu->Append(wxID_EXIT, "Exit");

        wxMenu* helpMenu = new wxMenu();
        helpMenu->Append(wxID_HELP, "Protocol Commands");

        wxMenuBar* menuBar = new wxMenuBar();
        menuBar->Append(fileMenu, "File");
        menuBar->Append(helpMenu, "Help");
        SetMenuBar(menuBar);
    }

    void BuildLayout() {
        wxPanel* root = new wxPanel(this);
        stylePanel(root, kCanvas);
        wxBoxSizer* rootSizer = new wxBoxSizer(wxVERTICAL);

        rootSizer->Add(BuildHeader(root), 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 16);
        rootSizer->Add(BuildConnectionBar(root), 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 10);

        notebook_ = new wxNotebook(root, wxID_ANY);
        notebook_->SetBackgroundColour(kCanvas);
        notebook_->SetForegroundColour(kInk);
        notebook_->SetFont(bodyFont(10, wxFONTWEIGHT_MEDIUM));
        notebook_->AddPage(BuildQueryPage(notebook_), "Student Query");
        notebook_->AddPage(BuildAdminPage(notebook_), "Admin Management");

        rootSizer->Add(notebook_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 10);
        rootSizer->Add(BuildRawLogPanel(root), 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, 10);
        root->SetSizer(rootSizer);

        CreateStatusBar();
        SetStatusText("Disconnected");
    }

    wxWindow* BuildHeader(wxWindow* parent) {
        wxPanel* panel = new wxPanel(parent);
        stylePanel(panel, kCanvas);

        wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);
        wxBoxSizer* titleSizer = new wxBoxSizer(wxVERTICAL);
        wxStaticText* title = new wxStaticText(panel, wxID_ANY, "Course Timetable");
        title->SetFont(displayFont(22));
        title->SetForegroundColour(kInk);
        wxStaticText* subtitle = new wxStaticText(panel,
                                                 wxID_ANY,
                                                 "Fast inquiry and administration console");
        subtitle->SetFont(bodyFont(10));
        subtitle->SetForegroundColour(kMuted);
        titleSizer->Add(title, 0, wxBOTTOM, 2);
        titleSizer->Add(subtitle, 0);

        sizer->Add(titleSizer, 0, wxALIGN_CENTER_VERTICAL);
        sizer->AddStretchSpacer(1);

        wxStaticText* wordmark = new wxStaticText(panel, wxID_ANY, "TIMETABLE");
        wordmark->SetFont(bodyFont(11, wxFONTWEIGHT_MEDIUM));
        wordmark->SetForegroundColour(kInk);
        sizer->Add(wordmark, 0, wxALIGN_CENTER_VERTICAL);

        panel->SetSizer(sizer);
        return panel;
    }

    wxSizer* BuildConnectionBar(wxWindow* parent) {
        wxStaticBoxSizer* sizer = new wxStaticBoxSizer(wxHORIZONTAL, parent, "Connection");
        styleStaticBox(sizer);

        sizer->Add(new wxStaticText(parent, wxID_ANY, "Host"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        hostCtrl_ = new wxTextCtrl(parent, wxID_ANY, "127.0.0.1", wxDefaultPosition, wxSize(150, -1));
        styleTextCtrl(hostCtrl_);
        sizer->Add(hostCtrl_, 0, wxRIGHT, 12);

        sizer->Add(new wxStaticText(parent, wxID_ANY, "Port"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        portCtrl_ = new wxSpinCtrl(parent, wxID_ANY);
        portCtrl_->SetRange(1, 65535);
        portCtrl_->SetValue(Protocol::DEFAULT_PORT);
        portCtrl_->SetBackgroundColour(kCanvas);
        portCtrl_->SetForegroundColour(kInk);
        portCtrl_->SetFont(bodyFont());
        sizer->Add(portCtrl_, 0, wxRIGHT, 12);

        sizer->Add(new wxStaticText(parent, wxID_ANY, "PSK"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        pskCtrl_ = new wxTextCtrl(parent,
                                  wxID_ANY,
                                  "",
                                  wxDefaultPosition,
                                  wxSize(180, -1),
                                  wxTE_PASSWORD);
        pskCtrl_->SetHint("optional; AES-GCM if set");
        styleTextCtrl(pskCtrl_);
        sizer->Add(pskCtrl_, 0, wxRIGHT, 12);

        connectButton_ = new wxButton(parent, wxID_ANY, "Connect");
        disconnectButton_ = new wxButton(parent, wxID_ANY, "Disconnect");
        styleButton(connectButton_, true);
        styleButton(disconnectButton_);
        disconnectButton_->SetMinSize(wxSize(124, 34));
        sizer->Add(connectButton_, 0, wxRIGHT, 6);
        sizer->Add(disconnectButton_, 0, wxRIGHT, 16);

        connectionLabel_ = new wxStaticText(parent, wxID_ANY, "Disconnected");
        connectionLabel_->SetFont(bodyFont(10, wxFONTWEIGHT_MEDIUM));
        connectionLabel_->SetForegroundColour(kMuted);
        sizer->Add(connectionLabel_, 0, wxALIGN_CENTER_VERTICAL);
        sizer->AddStretchSpacer(1);

        loginStateLabel_ = new wxStaticText(parent, wxID_ANY, "Role: Student");
        loginStateLabel_->SetFont(bodyFont(10, wxFONTWEIGHT_MEDIUM));
        loginStateLabel_->SetForegroundColour(kMuted);
        sizer->Add(loginStateLabel_, 0, wxALIGN_CENTER_VERTICAL);

        return sizer;
    }

    wxWindow* BuildQueryPage(wxWindow* parent) {
        wxPanel* panel = new wxPanel(parent);
        stylePanel(panel, kCanvas);
        wxBoxSizer* pageSizer = new wxBoxSizer(wxHORIZONTAL);
        wxPanel* formPanel = new wxPanel(panel);
        stylePanel(formPanel, kSurfaceCard);
        wxStaticBoxSizer* sizer = new wxStaticBoxSizer(wxVERTICAL, formPanel, "Student Query");
        styleStaticBox(sizer);

        wxStaticText* queryIntro = new wxStaticText(formPanel,
                                                   wxID_ANY,
                                                   "Filter course records without logging in.");
        queryIntro->SetForegroundColour(kMuted);
        queryIntro->SetFont(bodyFont(10));

        wxFlexGridSizer* grid = new wxFlexGridSizer(6, 2, 10, 10);
        grid->AddGrowableCol(1, 1);

        codeCtrl_ = new wxTextCtrl(formPanel, wxID_ANY);
        instructorCtrl_ = new wxTextCtrl(formPanel, wxID_ANY);
        semesterChoice_ = new wxChoice(formPanel, wxID_ANY);
        styleTextCtrl(codeCtrl_);
        styleTextCtrl(instructorCtrl_);
        styleChoice(semesterChoice_);
        const char* semesters[] = {"Any", "2025F", "2025S", "2026F", "2026S"};
        for (const char* semester : semesters) {
            semesterChoice_->Append(semester);
        }
        semesterChoice_->SetSelection(0);

        grid->Add(new wxStaticText(formPanel, wxID_ANY, "Course Code"), 0, wxALIGN_CENTER_VERTICAL);
        grid->Add(codeCtrl_, 1, wxEXPAND);

        grid->Add(new wxStaticText(formPanel, wxID_ANY, "Instructor"), 0, wxALIGN_CENTER_VERTICAL);
        grid->Add(instructorCtrl_, 1, wxEXPAND);

        grid->Add(new wxStaticText(formPanel, wxID_ANY, "Semester"), 0, wxALIGN_CENTER_VERTICAL);
        grid->Add(semesterChoice_, 1, wxEXPAND);

        dayChoice_ = new wxChoice(formPanel, wxID_ANY);
        styleChoice(dayChoice_);
        const char* days[] = {"Any", "Mon", "Tue", "Wed", "Thu", "Fri"};
        for (const char* day : days) {
            dayChoice_->Append(day);
        }
        dayChoice_->SetSelection(0);

        startChoice_ = new wxChoice(formPanel, wxID_ANY, wxDefaultPosition, wxSize(78, -1));
        endChoice_ = new wxChoice(formPanel, wxID_ANY, wxDefaultPosition, wxSize(78, -1));
        styleChoice(startChoice_);
        styleChoice(endChoice_);
        const char* times[] = {
            "08:00", "08:30", "09:00", "09:30", "10:00", "10:30",
            "11:00", "11:30", "12:00", "12:30", "13:00", "13:30",
            "14:00", "14:30", "15:00", "15:30", "16:00", "16:30",
            "17:00", "17:30", "18:00"
        };
        startChoice_->Append("Any");
        endChoice_->Append("Any");
        for (const char* time : times) {
            startChoice_->Append(time);
            endChoice_->Append(time);
        }
        startChoice_->SetSelection(0);
        endChoice_->SetSelection(0);

        grid->Add(new wxStaticText(formPanel, wxID_ANY, "Day"), 0, wxALIGN_CENTER_VERTICAL);
        grid->Add(dayChoice_, 1, wxEXPAND);
        grid->Add(new wxStaticText(formPanel, wxID_ANY, "Start Time"), 0, wxALIGN_CENTER_VERTICAL);
        grid->Add(startChoice_, 1, wxEXPAND);
        grid->Add(new wxStaticText(formPanel, wxID_ANY, "End Time"), 0, wxALIGN_CENTER_VERTICAL);
        grid->Add(endChoice_, 1, wxEXPAND);

        querySearchButton_ = new wxButton(formPanel, wxID_ANY, "Search");
        queryResetButton_ = new wxButton(formPanel, wxID_ANY, "Reset");
        styleButton(querySearchButton_, true);
        styleButton(queryResetButton_);
        wxBoxSizer* queryButtonSizer = new wxBoxSizer(wxHORIZONTAL);
        queryButtonSizer->Add(querySearchButton_, 1, wxRIGHT, 8);
        queryButtonSizer->Add(queryResetButton_, 0);

        sizer->Add(queryIntro, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 14);
        sizer->Add(grid, 0, wxEXPAND | wxALL, 14);
        sizer->Add(queryButtonSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 14);
        sizer->AddStretchSpacer(1);
        formPanel->SetMinSize(wxSize(360, -1));
        formPanel->SetSizer(sizer);

        pageSizer->Add(formPanel, 0, wxEXPAND | wxRIGHT, 16);
        pageSizer->Add(BuildResultPanel(panel), 1, wxEXPAND);
        panel->SetSizer(pageSizer);
        return panel;
    }

    wxWindow* BuildAdminPage(wxWindow* parent) {
        wxScrolledWindow* panel = new wxScrolledWindow(parent);
        stylePanel(panel, kCanvas);
        panel->SetScrollRate(8, 8);
        wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

        wxStaticBoxSizer* loginBox = new wxStaticBoxSizer(wxHORIZONTAL, panel, "Admin Login");
        styleStaticBox(loginBox);
        wxBoxSizer* loginSizer = new wxBoxSizer(wxHORIZONTAL);
        adminUserCtrl_ = new wxTextCtrl(panel, wxID_ANY, "admin", wxDefaultPosition, wxSize(180, -1));
        adminPasswordCtrl_ = new wxTextCtrl(panel, wxID_ANY, "", wxDefaultPosition, wxSize(180, -1), wxTE_PASSWORD);
        loginButton_ = new wxButton(panel, wxID_ANY, "Login");
        styleTextCtrl(adminUserCtrl_);
        styleTextCtrl(adminPasswordCtrl_);
        styleButton(loginButton_, true);

        loginSizer->Add(CreateFormLabel(panel, "User"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        loginSizer->Add(adminUserCtrl_, 0, wxRIGHT, 10);
        loginSizer->Add(CreateFormLabel(panel, "Password"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        loginSizer->Add(adminPasswordCtrl_, 0, wxRIGHT, 10);
        loginSizer->Add(loginButton_, 0);
        loginBox->Add(loginSizer, 1, wxEXPAND | wxALL, 12);
        sizer->Add(loginBox, 0, wxEXPAND | wxALL, 12);

        wxStaticBoxSizer* addBox = new wxStaticBoxSizer(wxVERTICAL, panel, "Add Course");
        styleStaticBox(addBox);
        wxStaticBoxSizer* addBasicBox = new wxStaticBoxSizer(wxVERTICAL, panel, "Basic Information");
        styleStaticBox(addBasicBox);
        wxFlexGridSizer* addBasicGrid = new wxFlexGridSizer(2, 4, 8, 8);
        addBasicGrid->AddGrowableCol(1, 1);
        addBasicGrid->AddGrowableCol(3, 1);
        addCodeCtrl_ = AddFormText(panel, addBasicGrid, "Course Code *");
        addTitleCtrl_ = AddFormText(panel, addBasicGrid, "Course Title *");
        addSectionCtrl_ = AddFormText(panel, addBasicGrid, "Section *");
        addInstructorCtrl_ = AddFormText(panel, addBasicGrid, "Instructor *");
        addBasicBox->Add(addBasicGrid, 0, wxEXPAND | wxALL, 10);
        addBox->Add(addBasicBox, 0, wxEXPAND | wxALL, 10);

        wxStaticBoxSizer* addScheduleBox = new wxStaticBoxSizer(wxVERTICAL, panel, "Schedule");
        styleStaticBox(addScheduleBox);
        wxFlexGridSizer* addScheduleGrid = new wxFlexGridSizer(3, 4, 8, 8);
        addScheduleGrid->AddGrowableCol(1, 1);
        addScheduleGrid->AddGrowableCol(3, 1);
        addSemesterChoice_ = AddFormChoice(panel, addScheduleGrid, "Semester *");
        AddSemesterChoices(addSemesterChoice_, true, "Select");
        addDayChoice_ = AddFormChoice(panel, addScheduleGrid, "Day *");
        AddDayChoices(addDayChoice_, true, "Select");
        addStartChoice_ = AddFormChoice(panel, addScheduleGrid, "Start Time *");
        AddTimeChoices(addStartChoice_, true, "Select");
        addEndChoice_ = AddFormChoice(panel, addScheduleGrid, "End Time *");
        AddTimeChoices(addEndChoice_, true, "Select");
        addClassroomCtrl_ = AddFormText(panel, addScheduleGrid, "Classroom");
        addScheduleGrid->AddSpacer(0);
        addScheduleGrid->AddSpacer(0);
        addScheduleBox->Add(addScheduleGrid, 0, wxEXPAND | wxALL, 10);
        addBox->Add(addScheduleBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);

        addResetButton_ = new wxButton(panel, wxID_ANY, "Reset");
        addButton_ = new wxButton(panel, wxID_ANY, "Add Course");
        styleButton(addResetButton_);
        styleButton(addButton_, true);
        wxBoxSizer* addButtonSizer = new wxBoxSizer(wxHORIZONTAL);
        addButtonSizer->AddStretchSpacer(1);
        addButtonSizer->Add(addResetButton_, 0, wxRIGHT, 8);
        addButtonSizer->Add(addButton_, 0);
        addBox->Add(addButtonSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
        sizer->Add(addBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);

        wxStaticBoxSizer* updateBox = new wxStaticBoxSizer(wxVERTICAL, panel, "Update Course");
        styleStaticBox(updateBox);
        wxBoxSizer* locatorSizer = new wxBoxSizer(wxHORIZONTAL);
        updateCodeCtrl_ = new wxTextCtrl(panel, wxID_ANY, "", wxDefaultPosition, wxSize(180, -1));
        updateSectionCtrl_ = new wxTextCtrl(panel, wxID_ANY, "", wxDefaultPosition, wxSize(120, -1));
        updateLoadSemesterChoice_ = new wxChoice(panel, wxID_ANY, wxDefaultPosition, wxSize(140, -1));
        AddSemesterChoices(updateLoadSemesterChoice_, true);
        updateLoadButton_ = new wxButton(panel, wxID_ANY, "Load Course");
        styleTextCtrl(updateCodeCtrl_);
        styleTextCtrl(updateSectionCtrl_);
        styleChoice(updateLoadSemesterChoice_);
        styleButton(updateLoadButton_);
        updateLoadButton_->SetMinSize(wxSize(124, 34));

        locatorSizer->Add(CreateFormLabel(panel, "Course Code"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        locatorSizer->Add(updateCodeCtrl_, 0, wxRIGHT, 10);
        locatorSizer->Add(CreateFormLabel(panel, "Section"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        locatorSizer->Add(updateSectionCtrl_, 0, wxRIGHT, 10);
        locatorSizer->Add(CreateFormLabel(panel, "Semester"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        locatorSizer->Add(updateLoadSemesterChoice_, 0, wxRIGHT, 10);
        locatorSizer->Add(updateLoadButton_, 0);
        updateBox->Add(locatorSizer, 0, wxEXPAND | wxALL, 10);

        wxFlexGridSizer* editGrid = new wxFlexGridSizer(4, 4, 8, 8);
        editGrid->AddGrowableCol(1, 1);
        editGrid->AddGrowableCol(3, 1);
        updateTitleCtrl_ = AddFormText(panel, editGrid, "Course Title");
        updateInstructorCtrl_ = AddFormText(panel, editGrid, "Instructor");
        updateDayChoice_ = AddFormChoice(panel, editGrid, "Day");
        AddDayChoices(updateDayChoice_, false);
        updateStartChoice_ = AddFormChoice(panel, editGrid, "Start Time");
        AddTimeChoices(updateStartChoice_, false);
        updateEndChoice_ = AddFormChoice(panel, editGrid, "End Time");
        AddTimeChoices(updateEndChoice_, false);
        updateClassroomCtrl_ = AddFormText(panel, editGrid, "Classroom");
        updateEditSemesterChoice_ = AddFormChoice(panel, editGrid, "Semester");
        AddSemesterChoices(updateEditSemesterChoice_, false);
        editGrid->AddSpacer(0);
        editGrid->AddSpacer(0);
        updateBox->Add(editGrid, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);

        wxBoxSizer* updateButtonSizer = new wxBoxSizer(wxHORIZONTAL);
        updateCancelButton_ = new wxButton(panel, wxID_ANY, "Cancel");
        updateSaveButton_ = new wxButton(panel, wxID_ANY, "Save Changes");
        styleButton(updateCancelButton_);
        styleButton(updateSaveButton_, true);
        updateButtonSizer->AddStretchSpacer(1);
        updateButtonSizer->Add(updateCancelButton_, 0, wxRIGHT, 8);
        updateButtonSizer->Add(updateSaveButton_, 0);
        updateBox->Add(updateButtonSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
        sizer->Add(updateBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);

        wxStaticBoxSizer* deleteBox = new wxStaticBoxSizer(wxVERTICAL, panel, "Delete Course");
        styleStaticBox(deleteBox);
        wxStaticBoxSizer* deleteFindBox = new wxStaticBoxSizer(wxVERTICAL, panel, "Find Course to Delete");
        styleStaticBox(deleteFindBox);
        wxBoxSizer* deleteFindSizer = new wxBoxSizer(wxHORIZONTAL);
        deleteCodeCtrl_ = new wxTextCtrl(panel, wxID_ANY, "", wxDefaultPosition, wxSize(180, -1));
        deleteSectionCtrl_ = new wxTextCtrl(panel, wxID_ANY, "", wxDefaultPosition, wxSize(120, -1));
        deleteSemesterChoice_ = new wxChoice(panel, wxID_ANY, wxDefaultPosition, wxSize(140, -1));
        AddSemesterChoices(deleteSemesterChoice_, true, "Select");
        deleteLoadButton_ = new wxButton(panel, wxID_ANY, "Load Course");
        styleTextCtrl(deleteCodeCtrl_);
        styleTextCtrl(deleteSectionCtrl_);
        styleChoice(deleteSemesterChoice_);
        styleButton(deleteLoadButton_);
        deleteLoadButton_->SetMinSize(wxSize(124, 34));
        deleteFindSizer->Add(CreateFormLabel(panel, "Course Code *"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        deleteFindSizer->Add(deleteCodeCtrl_, 0, wxRIGHT, 10);
        deleteFindSizer->Add(CreateFormLabel(panel, "Section *"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        deleteFindSizer->Add(deleteSectionCtrl_, 0, wxRIGHT, 10);
        deleteFindSizer->Add(CreateFormLabel(panel, "Semester *"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        deleteFindSizer->Add(deleteSemesterChoice_, 0, wxRIGHT, 10);
        deleteFindSizer->Add(deleteLoadButton_, 0);
        deleteFindBox->Add(deleteFindSizer, 0, wxEXPAND | wxALL, 10);
        deleteBox->Add(deleteFindBox, 0, wxEXPAND | wxALL, 10);

        wxStaticBoxSizer* deleteInfoBox = new wxStaticBoxSizer(wxVERTICAL, panel, "Course Information");
        styleStaticBox(deleteInfoBox);
        wxFlexGridSizer* deleteInfoGrid = new wxFlexGridSizer(5, 4, 8, 8);
        deleteInfoGrid->AddGrowableCol(1, 1);
        deleteInfoGrid->AddGrowableCol(3, 1);
        deleteInfoCodeCtrl_ = AddReadonlyFormText(panel, deleteInfoGrid, "Course Code");
        deleteInfoTitleCtrl_ = AddReadonlyFormText(panel, deleteInfoGrid, "Course Title");
        deleteInfoSectionCtrl_ = AddReadonlyFormText(panel, deleteInfoGrid, "Section");
        deleteInfoInstructorCtrl_ = AddReadonlyFormText(panel, deleteInfoGrid, "Instructor");
        deleteInfoDayCtrl_ = AddReadonlyFormText(panel, deleteInfoGrid, "Day");
        deleteInfoTimeCtrl_ = AddReadonlyFormText(panel, deleteInfoGrid, "Time");
        deleteInfoClassroomCtrl_ = AddReadonlyFormText(panel, deleteInfoGrid, "Classroom");
        deleteInfoSemesterCtrl_ = AddReadonlyFormText(panel, deleteInfoGrid, "Semester");
        deleteInfoBox->Add(deleteInfoGrid, 0, wxEXPAND | wxALL, 10);
        deleteWarningLabel_ = new wxStaticText(panel, wxID_ANY, "Warning: This action cannot be undone.");
        deleteWarningLabel_->SetForegroundColour(kError);
        deleteInfoBox->Add(deleteWarningLabel_, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);
        deleteBox->Add(deleteInfoBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);

        deleteCancelButton_ = new wxButton(panel, wxID_ANY, "Cancel");
        deleteButton_ = new wxButton(panel, wxID_ANY, "Delete Course");
        styleButton(deleteCancelButton_);
        styleButton(deleteButton_, true);
        wxBoxSizer* deleteButtonSizer = new wxBoxSizer(wxHORIZONTAL);
        deleteButtonSizer->AddStretchSpacer(1);
        deleteButtonSizer->Add(deleteCancelButton_, 0, wxRIGHT, 8);
        deleteButtonSizer->Add(deleteButton_, 0);
        deleteBox->Add(deleteButtonSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
        sizer->Add(deleteBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);

        sizer->AddStretchSpacer(1);
        panel->SetSizer(sizer);
        panel->FitInside();
        return panel;
    }

    wxWindow* BuildRawLogPanel(wxWindow* parent) {
        wxPanel* panel = new wxPanel(parent);
        stylePanel(panel, kCanvas);
        wxStaticBoxSizer* sizer = new wxStaticBoxSizer(wxVERTICAL, panel, "Live Protocol Log");
        styleStaticBox(sizer);

        rawResponseCtrl_ = new wxTextCtrl(panel,
                                          wxID_ANY,
                                          "",
                                          wxDefaultPosition,
                                          wxSize(-1, 96),
                                          wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP | wxHSCROLL);
        rawResponseCtrl_->SetBackgroundColour(kSurfaceSoft);
        rawResponseCtrl_->SetForegroundColour(kInk);
        rawResponseCtrl_->SetFont(wxFont(9,
                                         wxFONTFAMILY_TELETYPE,
                                         wxFONTSTYLE_NORMAL,
                                         wxFONTWEIGHT_NORMAL,
                                         false,
                                         "Consolas"));
        sizer->Add(rawResponseCtrl_, 1, wxEXPAND | wxALL, 8);

        panel->SetMinSize(wxSize(-1, 112));
        panel->SetSizer(sizer);
        return panel;
    }

    wxWindow* BuildResultPanel(wxWindow* parent) {
        wxPanel* panel = new wxPanel(parent);
        stylePanel(panel, kCanvas);
        wxStaticBoxSizer* sizer = new wxStaticBoxSizer(wxVERTICAL, panel, "Course Results");
        styleStaticBox(sizer);

        resultGrid_ = new wxGrid(panel, wxID_ANY);
        resultGrid_->SetDoubleBuffered(true);
        resultTable_ = new CourseGridTable();
        resultGrid_->SetTable(resultTable_, true, wxGrid::wxGridSelectRows);
        resultGrid_->EnableEditing(false);
        resultGrid_->SetRowLabelSize(0);
        resultGrid_->SetColLabelSize(34);
        resultGrid_->SetBackgroundColour(*wxWHITE);
        resultGrid_->SetDefaultCellBackgroundColour(*wxWHITE);
        resultGrid_->SetDefaultCellTextColour(kInk);
        resultGrid_->SetLabelBackgroundColour(kSurfaceSoft);
        resultGrid_->SetLabelTextColour(kInk);
        resultGrid_->SetGridLineColour(kHairline);
        resultGrid_->SetDefaultCellFont(bodyFont(10));
        resultGrid_->SetLabelFont(bodyFont(10, wxFONTWEIGHT_MEDIUM));
        resultGrid_->SetDefaultRowSize(30, true);
        resultGrid_->SetCellHighlightPenWidth(0);
        resultGrid_->SetCellHighlightROPenWidth(0);
        resultGrid_->DisableDragGridSize();
        resultGrid_->SetColSize(0, 96);
        resultGrid_->SetColSize(1, 260);
        resultGrid_->SetColSize(2, 74);
        resultGrid_->SetColSize(3, 175);
        resultGrid_->SetColSize(4, 70);
        resultGrid_->SetColSize(5, 78);
        resultGrid_->SetColSize(6, 78);
        resultGrid_->SetColSize(7, 100);
        resultGrid_->SetColSize(8, 90);

        sizer->Add(resultGrid_, 1, wxEXPAND | wxALL, 10);
        panel->SetSizer(sizer);
        return panel;
    }

    wxTextCtrl* AddLabeledText(wxWindow* parent, wxFlexGridSizer* sizer, const wxString& label) {
        wxTextCtrl* ctrl = new wxTextCtrl(parent, wxID_ANY);
        styleTextCtrl(ctrl);
        sizer->Add(new wxStaticText(parent, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
        sizer->Add(ctrl, 1, wxEXPAND);
        return ctrl;
    }

    wxStaticText* CreateFormLabel(wxWindow* parent, const wxString& label) {
        wxStaticText* text = new wxStaticText(parent, wxID_ANY, label);
        text->SetMinSize(wxSize(92, -1));
        text->SetFont(bodyFont(10, wxFONTWEIGHT_MEDIUM));
        text->SetForegroundColour(kBody);
        return text;
    }

    wxTextCtrl* AddFormText(wxWindow* parent, wxFlexGridSizer* sizer, const wxString& label) {
        wxTextCtrl* ctrl = new wxTextCtrl(parent, wxID_ANY);
        ctrl->SetMinSize(wxSize(180, -1));
        styleTextCtrl(ctrl);
        sizer->Add(CreateFormLabel(parent, label), 0, wxALIGN_CENTER_VERTICAL);
        sizer->Add(ctrl, 1, wxEXPAND);
        return ctrl;
    }

    wxTextCtrl* AddReadonlyFormText(wxWindow* parent, wxFlexGridSizer* sizer, const wxString& label) {
        wxTextCtrl* ctrl = new wxTextCtrl(parent,
                                         wxID_ANY,
                                         "",
                                         wxDefaultPosition,
                                         wxDefaultSize,
                                         wxTE_READONLY);
        ctrl->SetMinSize(wxSize(180, -1));
        styleTextCtrl(ctrl);
        sizer->Add(CreateFormLabel(parent, label), 0, wxALIGN_CENTER_VERTICAL);
        sizer->Add(ctrl, 1, wxEXPAND);
        return ctrl;
    }

    wxChoice* AddFormChoice(wxWindow* parent, wxFlexGridSizer* sizer, const wxString& label) {
        wxChoice* choice = new wxChoice(parent, wxID_ANY);
        choice->SetMinSize(wxSize(180, -1));
        styleChoice(choice);
        sizer->Add(CreateFormLabel(parent, label), 0, wxALIGN_CENTER_VERTICAL);
        sizer->Add(choice, 1, wxEXPAND);
        return choice;
    }

    void AddSemesterChoices(wxChoice* choice, bool includePlaceholder, const wxString& placeholder = "Any") {
        if (includePlaceholder) {
            choice->Append(placeholder);
        }
        const char* semesters[] = {"2025F", "2025S", "2026F", "2026S"};
        for (const char* semester : semesters) {
            choice->Append(semester);
        }
        choice->SetSelection(0);
    }

    void AddDayChoices(wxChoice* choice, bool includePlaceholder, const wxString& placeholder = "Any") {
        if (includePlaceholder) {
            choice->Append(placeholder);
        }
        const char* days[] = {"Mon", "Tue", "Wed", "Thu", "Fri"};
        for (const char* day : days) {
            choice->Append(day);
        }
        choice->SetSelection(0);
    }

    void AddTimeChoices(wxChoice* choice, bool includePlaceholder, const wxString& placeholder = "Any") {
        if (includePlaceholder) {
            choice->Append(placeholder);
        }
        const char* times[] = {
            "08:00", "08:30", "09:00", "09:30", "10:00", "10:30",
            "11:00", "11:30", "12:00", "12:30", "13:00", "13:30",
            "14:00", "14:30", "15:00", "15:30", "16:00", "16:30",
            "17:00", "17:30", "18:00"
        };
        for (const char* time : times) {
            choice->Append(time);
        }
        choice->SetSelection(0);
    }

    void BindEvents() {
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { Close(); }, wxID_EXIT);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { ShowProtocolHelp(); }, wxID_HELP);
        Bind(EVT_COMMAND_FINISHED, &MainFrame::OnCommandFinished, this);

        connectButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ConnectAsync(); });
        disconnectButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Disconnect(); });

        querySearchButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { RunSelectedQuery(); });
        queryResetButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ResetQueryControls(); });

        loginButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            if (RequireText(adminUserCtrl_, "Username") &&
                RequireText(adminPasswordCtrl_, "Password")) {
                RunCommandAsync("LOGIN " + wxToStd(adminUserCtrl_->GetValue()) + " " +
                                wxToStd(adminPasswordCtrl_->GetValue()));
            }
        });

        addResetButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ResetAddCourseForm(); });
        addButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { SubmitAddCourse(); });

        updateLoadButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { LoadCourseForUpdateAsync(); });
        updateCancelButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ClearLoadedCourse(); });
        updateSaveButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { SaveLoadedCourseChangesAsync(); });

        deleteLoadButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { LoadCourseForDeleteAsync(); });
        deleteCancelButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ClearDeleteCourse(); });
        deleteButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ConfirmAndDeleteLoadedCourse(); });

    }

    bool RequireText(wxTextCtrl* ctrl, const std::string& label) {
        if (!wxToStd(ctrl->GetValue()).empty()) {
            return true;
        }
        wxMessageBox(stdToWx(label + " cannot be empty."),
                     "Missing Value",
                     wxOK | wxICON_WARNING,
                     this);
        ctrl->SetFocus();
        return false;
    }

    bool IsPlaceholderSelection(wxChoice* choice) const {
        if (choice == nullptr || choice->GetSelection() == wxNOT_FOUND) {
            return true;
        }

        const std::string value = wxToStd(choice->GetStringSelection());
        return Protocol::iequals(value, "Any") || Protocol::iequals(value, "Select");
    }

    int MinutesFromChoice(wxChoice* choice) const {
        const std::string value = wxToStd(choice->GetStringSelection());
        if (value.size() != 5 ||
            value[2] != ':' ||
            !std::isdigit(static_cast<unsigned char>(value[0])) ||
            !std::isdigit(static_cast<unsigned char>(value[1])) ||
            !std::isdigit(static_cast<unsigned char>(value[3])) ||
            !std::isdigit(static_cast<unsigned char>(value[4]))) {
            return -1;
        }

        const int hours = std::stoi(value.substr(0, 2));
        const int minutes = std::stoi(value.substr(3, 2));
        if (hours < 0 || hours > 23 || minutes < 0 || minutes > 59) {
            return -1;
        }
        return hours * 60 + minutes;
    }

    bool ValidateAddText(wxTextCtrl* ctrl, const std::string& message, std::string& value) {
        value = Protocol::trim(wxToStd(ctrl->GetValue()));
        if (!value.empty()) {
            ctrl->SetValue(stdToWx(value));
            return true;
        }

        wxMessageBox(stdToWx(message), "Missing Value", wxOK | wxICON_WARNING, this);
        SetStatusText(stdToWx(message));
        ctrl->SetFocus();
        return false;
    }

    bool ValidateAddChoice(wxChoice* choice, const std::string& message) {
        if (!IsPlaceholderSelection(choice)) {
            return true;
        }

        wxMessageBox(stdToWx(message), "Missing Value", wxOK | wxICON_WARNING, this);
        SetStatusText(stdToWx(message));
        choice->SetFocus();
        return false;
    }

    void SubmitAddCourse() {
        std::string code;
        std::string title;
        std::string section;
        std::string instructor;
        std::string classroom;

        if (!ValidateAddText(addCodeCtrl_, "Course Code is required.", code) ||
            !ValidateAddText(addTitleCtrl_, "Course Title is required.", title) ||
            !ValidateAddText(addSectionCtrl_, "Section is required.", section) ||
            !ValidateAddText(addInstructorCtrl_, "Instructor is required.", instructor) ||
            !ValidateAddChoice(addSemesterChoice_, "Please select a semester.") ||
            !ValidateAddChoice(addDayChoice_, "Please select a day.") ||
            !ValidateAddChoice(addStartChoice_, "Please select a start time.") ||
            !ValidateAddChoice(addEndChoice_, "Please select an end time.")) {
            return;
        }

        classroom = Protocol::trim(wxToStd(addClassroomCtrl_->GetValue()));
        addClassroomCtrl_->SetValue(stdToWx(classroom));

        const int startMinutes = MinutesFromChoice(addStartChoice_);
        const int endMinutes = MinutesFromChoice(addEndChoice_);
        if (startMinutes < 0 || endMinutes < 0 || endMinutes <= startMinutes) {
            wxMessageBox("End Time must be later than Start Time.",
                         "Invalid Time",
                         wxOK | wxICON_WARNING,
                         this);
            SetStatusText("End Time must be later than Start Time.");
            addEndChoice_->SetFocus();
            return;
        }

        code = Protocol::toUpper(code);
        addCodeCtrl_->SetValue(stdToWx(code));

        RunCommandAsync("ADD " + code + "|" +
                        title + "|" +
                        section + "|" +
                        instructor + "|" +
                        wxToStd(addDayChoice_->GetStringSelection()) + "|" +
                        wxToStd(addStartChoice_->GetStringSelection()) + "|" +
                        wxToStd(addEndChoice_->GetStringSelection()) + "|" +
                        classroom + "|" +
                        wxToStd(addSemesterChoice_->GetStringSelection()));
    }

    void ResetAddCourseForm() {
        addCodeCtrl_->Clear();
        addTitleCtrl_->Clear();
        addSectionCtrl_->Clear();
        addInstructorCtrl_->Clear();
        addClassroomCtrl_->Clear();
        addSemesterChoice_->SetSelection(0);
        addDayChoice_->SetSelection(0);
        addStartChoice_->SetSelection(0);
        addEndChoice_->SetSelection(0);
        SetStatusText("Add Course form reset.");
        addCodeCtrl_->SetFocus();
    }

    void ClearAddCourseTextFields() {
        addCodeCtrl_->Clear();
        addTitleCtrl_->Clear();
        addSectionCtrl_->Clear();
        addInstructorCtrl_->Clear();
        addClassroomCtrl_->Clear();
        addCodeCtrl_->SetFocus();
    }

    void SendCommandFromInput(const std::string& command, wxTextCtrl* ctrl) {
        if (RequireText(ctrl, command)) {
            RunCommandAsync(command + " " + wxToStd(ctrl->GetValue()));
        }
    }

    void RunSelectedQuery() {
        const std::string start = ChoiceFilterValue(startChoice_);
        const std::string end = ChoiceFilterValue(endChoice_);

        RunCommandAsync("QUERY_FILTER " +
                        wxToStd(codeCtrl_->GetValue()) + "|" +
                        wxToStd(instructorCtrl_->GetValue()) + "|" +
                        ChoiceFilterValue(semesterChoice_) + "|" +
                        ChoiceFilterValue(dayChoice_) + "|" +
                        start + "|" + end);
    }

    void UpdateQueryControls() {
        if (codeCtrl_ == nullptr) {
            return;
        }

        const bool enabled = client_.isConnected() && !busy_;
        codeCtrl_->Enable(enabled);
        instructorCtrl_->Enable(enabled);
        semesterChoice_->Enable(enabled);
        dayChoice_->Enable(enabled);
        startChoice_->Enable(enabled);
        endChoice_->Enable(enabled);

        if (querySearchButton_ != nullptr) {
            querySearchButton_->Enable(enabled);
        }
        if (queryResetButton_ != nullptr) {
            queryResetButton_->Enable(enabled);
        }
    }

    std::string ChoiceFilterValue(wxChoice* choice) const {
        if (choice == nullptr || choice->GetSelection() == wxNOT_FOUND) {
            return "";
        }

        const std::string value = wxToStd(choice->GetStringSelection());
        return Protocol::iequals(value, "Any") ? "" : value;
    }

    void ResetQueryControls() {
        codeCtrl_->Clear();
        instructorCtrl_->Clear();
        semesterChoice_->SetSelection(0);
        dayChoice_->SetSelection(0);
        startChoice_->SetSelection(0);
        endChoice_->SetSelection(0);
        codeCtrl_->SetFocus();
    }

    bool SetChoiceByValue(wxChoice* choice, const std::string& value) {
        const int index = choice->FindString(stdToWx(value), false);
        if (index != wxNOT_FOUND) {
            choice->SetSelection(index);
            return true;
        }

        choice->Append(stdToWx(value));
        choice->SetSelection(choice->GetCount() - 1);
        return true;
    }

    void ClearLoadedCourse() {
        hasLoadedCourse_ = false;
        loadedCourse_ = CourseRow{};
        updateTitleCtrl_->Clear();
        updateInstructorCtrl_->Clear();
        updateDayChoice_->SetSelection(0);
        updateStartChoice_->SetSelection(0);
        updateEndChoice_->SetSelection(0);
        updateClassroomCtrl_->Clear();
        updateEditSemesterChoice_->SetSelection(0);
        UpdateAdminState();
    }

    void PopulateUpdateEditor(const CourseRow& course) {
        loadedCourse_ = course;
        hasLoadedCourse_ = true;

        updateTitleCtrl_->SetValue(stdToWx(course.title));
        updateInstructorCtrl_->SetValue(stdToWx(course.instructor));
        SetChoiceByValue(updateDayChoice_, course.day);
        SetChoiceByValue(updateStartChoice_, course.start);
        SetChoiceByValue(updateEndChoice_, course.end);
        updateClassroomCtrl_->SetValue(stdToWx(course.classroom));
        SetChoiceByValue(updateEditSemesterChoice_, course.semester);

        UpdateAdminState();
    }

    void LoadCourseForUpdateAsync() {
        if (busy_) {
            return;
        }

        if (!client_.isConnected()) {
            wxMessageBox("Connect to the server first.", "Not Connected", wxOK | wxICON_WARNING, this);
            return;
        }

        if (!RequireText(updateCodeCtrl_, "CourseCode") ||
            !RequireText(updateSectionCtrl_, "Section")) {
            return;
        }

        if (worker_.joinable()) {
            worker_.join();
        }

        ClearLoadedCourse();

        const std::string payload = wxToStd(updateCodeCtrl_->GetValue()) + "|" +
                                    wxToStd(updateSectionCtrl_->GetValue()) + "|" +
                                    ChoiceFilterValue(updateLoadSemesterChoice_);
        AppendSendLog("GET_COURSE " + payload);
        busy_ = true;
        SetControlsEnabled(false);
        SetStatusText("Loading course...");

        worker_ = std::thread([this, payload]() {
            ServerResponse response = client_.sendCommand("GET_COURSE " + payload);
            QueueResult(response, "LOAD_COURSE " + payload);
        });
    }

    void SaveLoadedCourseChangesAsync() {
        if (busy_) {
            return;
        }

        if (!hasLoadedCourse_) {
            wxMessageBox("Load a course first.", "No Course Loaded", wxOK | wxICON_WARNING, this);
            return;
        }

        CourseRow edited = loadedCourse_;
        edited.title = wxToStd(updateTitleCtrl_->GetValue());
        edited.instructor = wxToStd(updateInstructorCtrl_->GetValue());
        edited.day = wxToStd(updateDayChoice_->GetStringSelection());
        edited.start = wxToStd(updateStartChoice_->GetStringSelection());
        edited.end = wxToStd(updateEndChoice_->GetStringSelection());
        edited.classroom = wxToStd(updateClassroomCtrl_->GetValue());
        edited.semester = wxToStd(updateEditSemesterChoice_->GetStringSelection());

        if (Protocol::trim(edited.title).empty() ||
            Protocol::trim(edited.instructor).empty() ||
            Protocol::trim(edited.classroom).empty() ||
            Protocol::trim(edited.semester).empty()) {
            wxMessageBox("Edited fields cannot be empty.", "Missing Value", wxOK | wxICON_WARNING, this);
            return;
        }

        std::vector<std::pair<std::string, std::string>> changes;
        if (edited.title != loadedCourse_.title) {
            changes.push_back({"CourseTitle", edited.title});
        }
        if (edited.instructor != loadedCourse_.instructor) {
            changes.push_back({"Instructor", edited.instructor});
        }
        if (edited.day != loadedCourse_.day ||
            edited.start != loadedCourse_.start ||
            edited.end != loadedCourse_.end) {
            changes.push_back({"Time", edited.day + " " + edited.start + " " + edited.end});
        }
        if (edited.classroom != loadedCourse_.classroom) {
            changes.push_back({"Classroom", edited.classroom});
        }
        if (edited.semester != loadedCourse_.semester) {
            changes.push_back({"Semester", edited.semester});
        }

        if (changes.empty()) {
            wxMessageBox("No changes to save.", "No Changes", wxOK | wxICON_INFORMATION, this);
            return;
        }

        if (worker_.joinable()) {
            worker_.join();
        }

        const std::string courseCode = loadedCourse_.code;
        const std::string section = loadedCourse_.section;
        const std::string semester = loadedCourse_.semester;
        std::vector<std::string> updateCommands;
        updateCommands.reserve(changes.size());
        for (const auto& change : changes) {
            updateCommands.push_back("UPDATE " + courseCode + " " + section + " " +
                                     semester + " " +
                                     change.first + " " + change.second);
        }

        for (const std::string& command : updateCommands) {
            AppendSendLog(command);
        }

        busy_ = true;
        SetControlsEnabled(false);
        SetStatusText("Saving changes...");

        worker_ = std::thread([this, updateCommands]() {
            ServerResponse finalResponse;
            finalResponse.ok = true;
            finalResponse.connected = client_.isConnected();

            for (const std::string& command : updateCommands) {
                ServerResponse response = client_.sendCommand(command);
                finalResponse.connected = response.connected;
                finalResponse.bodyLines.push_back(response.firstLine);
                if (!response.ok ||
                    Protocol::startsWithIgnoreCase(response.firstLine, "ERROR") ||
                    Protocol::startsWithIgnoreCase(response.firstLine, "FAILURE")) {
                    finalResponse.ok = response.ok;
                    finalResponse.error = response.error;
                    finalResponse.firstLine = response.ok ? response.firstLine : response.error;
                    QueueResult(finalResponse, "UPDATE_BATCH");
                    return;
                }
            }

            finalResponse.firstLine = "OK Course updated successfully.";
            QueueResult(finalResponse, "UPDATE_BATCH");
        });
    }

    bool ValidateDeleteLocator(std::string& code, std::string& section, std::string& semester) {
        code = Protocol::trim(wxToStd(deleteCodeCtrl_->GetValue()));
        section = Protocol::trim(wxToStd(deleteSectionCtrl_->GetValue()));

        if (code.empty()) {
            wxMessageBox("Course Code is required.", "Missing Value", wxOK | wxICON_WARNING, this);
            SetStatusText("Course Code is required.");
            deleteCodeCtrl_->SetFocus();
            return false;
        }

        if (section.empty()) {
            wxMessageBox("Section is required.", "Missing Value", wxOK | wxICON_WARNING, this);
            SetStatusText("Section is required.");
            deleteSectionCtrl_->SetFocus();
            return false;
        }

        if (IsPlaceholderSelection(deleteSemesterChoice_)) {
            wxMessageBox("Please select a semester.", "Missing Value", wxOK | wxICON_WARNING, this);
            SetStatusText("Please select a semester.");
            deleteSemesterChoice_->SetFocus();
            return false;
        }

        code = Protocol::toUpper(code);
        semester = wxToStd(deleteSemesterChoice_->GetStringSelection());
        deleteCodeCtrl_->SetValue(stdToWx(code));
        deleteSectionCtrl_->SetValue(stdToWx(section));
        return true;
    }

    void ClearDeleteCourse() {
        hasDeleteCourse_ = false;
        deleteCourse_ = CourseRow{};
        deleteInfoCodeCtrl_->Clear();
        deleteInfoTitleCtrl_->Clear();
        deleteInfoSectionCtrl_->Clear();
        deleteInfoInstructorCtrl_->Clear();
        deleteInfoDayCtrl_->Clear();
        deleteInfoTimeCtrl_->Clear();
        deleteInfoClassroomCtrl_->Clear();
        deleteInfoSemesterCtrl_->Clear();
        UpdateAdminState();
    }

    void PopulateDeleteInfo(const CourseRow& course) {
        deleteCourse_ = course;
        hasDeleteCourse_ = true;
        deleteInfoCodeCtrl_->SetValue(stdToWx(course.code));
        deleteInfoTitleCtrl_->SetValue(stdToWx(course.title));
        deleteInfoSectionCtrl_->SetValue(stdToWx(course.section));
        deleteInfoInstructorCtrl_->SetValue(stdToWx(course.instructor));
        deleteInfoDayCtrl_->SetValue(stdToWx(course.day));
        deleteInfoTimeCtrl_->SetValue(stdToWx(course.start + " - " + course.end));
        deleteInfoClassroomCtrl_->SetValue(stdToWx(course.classroom));
        deleteInfoSemesterCtrl_->SetValue(stdToWx(course.semester));
        UpdateAdminState();
    }

    void LoadCourseForDeleteAsync() {
        if (busy_) {
            return;
        }

        if (!client_.isConnected()) {
            wxMessageBox("Connect to the server first.", "Not Connected", wxOK | wxICON_WARNING, this);
            return;
        }

        std::string code;
        std::string section;
        std::string semester;
        if (!ValidateDeleteLocator(code, section, semester)) {
            return;
        }

        if (worker_.joinable()) {
            worker_.join();
        }

        ClearDeleteCourse();

        const std::string payload = code + "|" + section + "|" + semester;
        AppendSendLog("GET_COURSE " + payload);
        busy_ = true;
        SetControlsEnabled(false);
        SetStatusText("Loading course...");

        worker_ = std::thread([this, payload]() {
            ServerResponse response = client_.sendCommand("GET_COURSE " + payload);
            QueueResult(response, "LOAD_DELETE_COURSE " + payload);
        });
    }

    void ConfirmAndDeleteLoadedCourse() {
        if (!hasDeleteCourse_) {
            wxMessageBox("Load a course first.", "No Course Loaded", wxOK | wxICON_WARNING, this);
            return;
        }

        const std::string message =
            "Are you sure you want to delete this course?\n\n"
            "Course Code: " + deleteCourse_.code + "\n"
            "Section: " + deleteCourse_.section + "\n"
            "Title: " + deleteCourse_.title + "\n"
            "Semester: " + deleteCourse_.semester + "\n\n"
            "This action cannot be undone.";

        const int answer = wxMessageBox(stdToWx(message),
                                        "Confirm Delete",
                                        wxYES_NO | wxNO_DEFAULT | wxICON_WARNING,
                                        this);
        if (answer != wxYES) {
            return;
        }

        RunCommandAsync("DELETE " + deleteCourse_.code + " " +
                        deleteCourse_.section + " " + deleteCourse_.semester);
    }

    void ConnectAsync() {
        if (busy_) {
            return;
        }

        const std::string host = wxToStd(hostCtrl_->GetValue());
        const int port = portCtrl_->GetValue();
        const std::string psk = wxToStd(pskCtrl_->GetValue());

        if (host.empty()) {
            wxMessageBox("Host cannot be empty.", "Missing Value", wxOK | wxICON_WARNING, this);
            return;
        }

        if (worker_.joinable()) {
            worker_.join();
        }

        busy_ = true;
        SetControlsEnabled(false);
        SetStatusText("Connecting...");
        connectionLabel_->SetLabel("Connecting...");
        AppendSendLog("CONNECT " + host + ":" + std::to_string(port) +
                      (psk.empty() ? "" : " (secure)"));

        worker_ = std::thread([this, host, port, psk]() {
            ServerResponse response = client_.connectTo(host, port, psk);
            QueueResult(response, "CONNECT");
        });
    }

    void RunCommandAsync(const std::string& command) {
        if (busy_) {
            return;
        }

        if (!client_.isConnected()) {
            wxMessageBox("Connect to the server first.", "Not Connected", wxOK | wxICON_WARNING, this);
            return;
        }

        if (worker_.joinable()) {
            worker_.join();
        }

        busy_ = true;
        SetControlsEnabled(false);
        SetStatusText("Sending command...");
        AppendSendLog(command);

        worker_ = std::thread([this, command]() {
            ServerResponse response = client_.sendCommand(command);
            QueueResult(response, command);
        });
    }

    void QueueResult(const ServerResponse& response, const std::string& command) {
        if (closing_) {
            return;
        }

        wxThreadEvent* event = new wxThreadEvent(EVT_COMMAND_FINISHED);
        event->SetString(stdToWx(command));
        event->SetInt(response.connected ? 1 : 0);
        event->SetPayload<ServerResponse>(response);
        wxQueueEvent(this, event);
    }

    void OnCommandFinished(wxThreadEvent& event) {
        busy_ = false;
        if (worker_.joinable()) {
            worker_.join();
        }

        const ServerResponse response = event.GetPayload<ServerResponse>();
        const std::string command = wxToStd(event.GetString());
        const bool connected = event.GetInt() == 1 && client_.isConnected();

        UpdateConnectionState(connected);
        SetControlsEnabled(true);

        if (!response.ok) {
            AppendRecvLog(response);
            SetStatusText(stdToWx(response.error));
            return;
        }

        AppendRecvLog(response);
        SetStatusText(stdToWx(response.firstLine));

        if (Protocol::startsWithIgnoreCase(command, "LOAD_COURSE ")) {
            if (Protocol::startsWithIgnoreCase(response.firstLine, "ERROR")) {
                wxMessageBox("Course not found.", "Load Course", wxOK | wxICON_WARNING, this);
                ClearLoadedCourse();
                return;
            }

            CourseRow course;
            if (response.bodyLines.empty() || !parseCoursePayloadRow(response.bodyLines.front(), course)) {
                wxMessageBox("Course not found.", "Load Course", wxOK | wxICON_WARNING, this);
                ClearLoadedCourse();
                return;
            }

            PopulateUpdateEditor(course);
            SetStatusText("Course loaded.");
            return;
        }

        if (Protocol::startsWithIgnoreCase(command, "LOAD_DELETE_COURSE ")) {
            if (Protocol::startsWithIgnoreCase(response.firstLine, "ERROR")) {
                wxMessageBox("Course not found.", "Load Course", wxOK | wxICON_WARNING, this);
                ClearDeleteCourse();
                return;
            }

            CourseRow course;
            if (response.bodyLines.empty() || !parseCoursePayloadRow(response.bodyLines.front(), course)) {
                wxMessageBox("Course not found.", "Load Course", wxOK | wxICON_WARNING, this);
                ClearDeleteCourse();
                return;
            }

            PopulateDeleteInfo(course);
            SetStatusText("Course loaded for deletion.");
            return;
        }

        if (command == "UPDATE_BATCH") {
            if (Protocol::startsWithIgnoreCase(response.firstLine, "OK")) {
                loadedCourse_.title = wxToStd(updateTitleCtrl_->GetValue());
                loadedCourse_.instructor = wxToStd(updateInstructorCtrl_->GetValue());
                loadedCourse_.day = wxToStd(updateDayChoice_->GetStringSelection());
                loadedCourse_.start = wxToStd(updateStartChoice_->GetStringSelection());
                loadedCourse_.end = wxToStd(updateEndChoice_->GetStringSelection());
                loadedCourse_.classroom = wxToStd(updateClassroomCtrl_->GetValue());
                loadedCourse_.semester = wxToStd(updateEditSemesterChoice_->GetStringSelection());
                wxMessageBox("Course updated successfully.",
                             "Update Course",
                             wxOK | wxICON_INFORMATION,
                             this);
            } else {
                wxMessageBox(stdToWx(response.firstLine),
                             "Update Course",
                             wxOK | wxICON_WARNING,
                             this);
            }
            return;
        }

        if (Protocol::startsWithIgnoreCase(command, "EXIT") ||
            Protocol::startsWithIgnoreCase(response.firstLine, "OK Goodbye.")) {
            client_.disconnect();
            isAdmin_ = false;
            UpdateConnectionState(false);
            UpdateAdminState();
            return;
        }

        if (Protocol::startsWithIgnoreCase(response.firstLine, "SUCCESS")) {
            isAdmin_ = true;
            UpdateAdminState();
        }

        if (Protocol::startsWithIgnoreCase(response.firstLine, "RESULT ")) {
            std::vector<CourseRow> rows = parseCourseRows(response);
            if (rows.empty()) {
                SetStatusText("No matching courses.");
            }
            PopulateGrid(std::move(rows));
        }

        if (Protocol::startsWithIgnoreCase(command, "ADD ")) {
            if (Protocol::startsWithIgnoreCase(response.firstLine, "OK")) {
                wxMessageBox("Course added successfully.",
                             "Add Course",
                             wxOK | wxICON_INFORMATION,
                             this);
                ClearAddCourseTextFields();
            } else {
                wxMessageBox(stdToWx(response.firstLine),
                             "Add Course",
                             wxOK | wxICON_WARNING,
                             this);
            }
            return;
        }

        if (Protocol::startsWithIgnoreCase(command, "UPDATE ") ||
            Protocol::startsWithIgnoreCase(command, "DELETE ")) {
            if (Protocol::startsWithIgnoreCase(command, "DELETE ")) {
                if (Protocol::startsWithIgnoreCase(response.firstLine, "OK")) {
                    wxMessageBox("Course deleted successfully.",
                                 "Delete Course",
                                 wxOK | wxICON_INFORMATION,
                                 this);
                    ClearDeleteCourse();
                } else {
                    wxMessageBox(stdToWx(response.firstLine),
                                 "Delete Course",
                                 wxOK | wxICON_WARNING,
                                 this);
                }
            } else {
                wxMessageBox(stdToWx(response.firstLine), "Server Response", wxOK | wxICON_INFORMATION, this);
            }
        }
    }

    void AppendRawLog(const wxString& message) {
        if (rawResponseCtrl_ == nullptr) {
            return;
        }

        wxString line = "[" + wxDateTime::Now().FormatISOTime() + "] " + message;
        if (!line.EndsWith("\n")) {
            line += "\n";
        }

        rawLogLines_.push_back(line);
        const bool needsTrim = rawLogLines_.size() > kMaxRawLogLines;
        if (needsTrim) {
            rawLogLines_.erase(rawLogLines_.begin(),
                               rawLogLines_.begin() + (rawLogLines_.size() - kMaxRawLogLines));

            wxString text;
            for (const wxString& item : rawLogLines_) {
                text += item;
            }
            rawResponseCtrl_->ChangeValue(text);
        } else {
            rawResponseCtrl_->AppendText(line);
        }
        rawResponseCtrl_->ShowPosition(rawResponseCtrl_->GetLastPosition());
    }

    void AppendSendLog(const std::string& command) {
        AppendRawLog(wxString("SEND: ") + stdToWx(command));
    }

    void AppendRecvLog(const ServerResponse& response) {
        std::string text;
        if (!response.ok) {
            text = response.error;
        } else {
            text = response.firstLine;
            const std::size_t previewCount = std::min<std::size_t>(response.bodyLines.size(), 12);
            for (std::size_t index = 0; index < previewCount; ++index) {
                text += "\n" + response.bodyLines[index];
            }
            if (response.bodyLines.size() > previewCount) {
                text += "\n... " + std::to_string(response.bodyLines.size() - previewCount) +
                        " more lines hidden from the live log";
            }
        }
        if (text.empty()) {
            text = response.firstLine;
        }
        AppendRawLog(wxString("RECV: ") + stdToWx(text));
    }

    void Disconnect() {
        if (client_.isConnected()) {
            AppendSendLog("DISCONNECT");
        }
        client_.disconnect();
        AppendRawLog("RECV: Disconnected.");
        isAdmin_ = false;
        UpdateConnectionState(false);
        UpdateAdminState();
        SetStatusText("Disconnected");
    }

    void UpdateConnectionState(bool connected) {
        connectionLabel_->SetLabel(connected ? (client_.isSecure() ? "Connected (encrypted)" : "Connected")
                                           : "Disconnected");
        connectionLabel_->SetForegroundColour(connected ? kSuccess : kMuted);
        connectButton_->Enable(!connected && !busy_);
        disconnectButton_->Enable(connected && !busy_);
        hostCtrl_->Enable(!connected && !busy_);
        portCtrl_->Enable(!connected && !busy_);
        pskCtrl_->Enable(!connected && !busy_);
        UpdateQueryControls();
        UpdateAdminState();
    }

    void UpdateAdminState() {
        loginStateLabel_->SetLabel(isAdmin_ ? "Role: Admin" : "Role: Student");
        loginStateLabel_->SetForegroundColour(isAdmin_ ? kSuccess : kMuted);

        const bool adminEnabled = isAdmin_ && client_.isConnected() && !busy_;
        SetAdminControlsEnabled(adminEnabled);
    }

    void SetAdminControlsEnabled(bool enabled) {
        wxWindow* controls[] = {
            addCodeCtrl_, addTitleCtrl_, addSectionCtrl_, addInstructorCtrl_,
            addClassroomCtrl_, addSemesterChoice_, addDayChoice_, addStartChoice_,
            addEndChoice_, addResetButton_, addButton_,
            updateCodeCtrl_, updateSectionCtrl_, updateLoadSemesterChoice_, updateLoadButton_,
            deleteCodeCtrl_, deleteSectionCtrl_, deleteSemesterChoice_, deleteLoadButton_,
            deleteCancelButton_
        };

        for (wxWindow* control : controls) {
            if (control != nullptr) {
                control->Enable(enabled);
            }
        }

        SetUpdateEditorEnabled(enabled && hasLoadedCourse_);
        SetDeleteCourseControlsEnabled(enabled && hasDeleteCourse_);
    }

    void SetUpdateEditorEnabled(bool enabled) {
        wxWindow* controls[] = {
            updateTitleCtrl_, updateInstructorCtrl_, updateDayChoice_,
            updateStartChoice_, updateEndChoice_, updateClassroomCtrl_,
            updateEditSemesterChoice_, updateCancelButton_, updateSaveButton_
        };

        for (wxWindow* control : controls) {
            if (control != nullptr) {
                control->Enable(enabled);
            }
        }
    }

    void SetDeleteCourseControlsEnabled(bool enabled) {
        wxWindow* infoControls[] = {
            deleteInfoCodeCtrl_, deleteInfoTitleCtrl_, deleteInfoSectionCtrl_,
            deleteInfoInstructorCtrl_, deleteInfoDayCtrl_, deleteInfoTimeCtrl_,
            deleteInfoClassroomCtrl_, deleteInfoSemesterCtrl_, deleteWarningLabel_,
            deleteButton_
        };

        for (wxWindow* control : infoControls) {
            if (control != nullptr) {
                control->Enable(enabled);
            }
        }
    }

    void SetControlsEnabled(bool enabled) {
        const bool connected = client_.isConnected();
        connectButton_->Enable(enabled && !connected);
        disconnectButton_->Enable(enabled && connected);
        hostCtrl_->Enable(enabled && !connected);
        portCtrl_->Enable(enabled && !connected);
        pskCtrl_->Enable(enabled && !connected);

        UpdateQueryControls();
        loginButton_->Enable(enabled && connected);
        UpdateAdminState();
    }

    void PopulateGrid(std::vector<CourseRow> rows) {
        if (resultGrid_ == nullptr || resultTable_ == nullptr) {
            return;
        }

        const int oldRowCount = resultTable_->GetNumberRows();
        const int newRowCount = static_cast<int>(rows.size());

        resultGrid_->Freeze();
        resultGrid_->BeginBatch();

        resultTable_->SetRows(std::move(rows));

        if (newRowCount < oldRowCount) {
            wxGridTableMessage msg(resultTable_,
                                   wxGRIDTABLE_NOTIFY_ROWS_DELETED,
                                   newRowCount,
                                   oldRowCount - newRowCount);
            resultGrid_->ProcessTableMessage(msg);
        } else if (newRowCount > oldRowCount) {
            wxGridTableMessage msg(resultTable_,
                                   wxGRIDTABLE_NOTIFY_ROWS_APPENDED,
                                   newRowCount - oldRowCount);
            resultGrid_->ProcessTableMessage(msg);
        }

        resultGrid_->ClearSelection();
        resultGrid_->Refresh();
        resultGrid_->EndBatch();
        resultGrid_->Thaw();
    }

    void ShowProtocolHelp() {
        wxMessageBox(stdToWx(Protocol::protocolHelp()),
                     "Protocol Commands",
                     wxOK | wxICON_INFORMATION,
                     this);
    }

    CourseTcpClient client_;
    std::thread worker_;
    std::atomic_bool closing_{false};
    bool busy_ = false;
    bool isAdmin_ = false;
    bool hasLoadedCourse_ = false;
    bool hasDeleteCourse_ = false;
    CourseRow loadedCourse_;
    CourseRow deleteCourse_;

    wxNotebook* notebook_ = nullptr;
    wxTextCtrl* hostCtrl_ = nullptr;
    wxSpinCtrl* portCtrl_ = nullptr;
    wxTextCtrl* pskCtrl_ = nullptr;
    wxButton* connectButton_ = nullptr;
    wxButton* disconnectButton_ = nullptr;
    wxStaticText* connectionLabel_ = nullptr;
    wxStaticText* loginStateLabel_ = nullptr;

    wxTextCtrl* codeCtrl_ = nullptr;
    wxTextCtrl* instructorCtrl_ = nullptr;
    wxChoice* semesterChoice_ = nullptr;
    wxChoice* dayChoice_ = nullptr;
    wxChoice* startChoice_ = nullptr;
    wxChoice* endChoice_ = nullptr;
    wxButton* querySearchButton_ = nullptr;
    wxButton* queryResetButton_ = nullptr;

    wxTextCtrl* adminUserCtrl_ = nullptr;
    wxTextCtrl* adminPasswordCtrl_ = nullptr;
    wxButton* loginButton_ = nullptr;
    wxTextCtrl* addCodeCtrl_ = nullptr;
    wxTextCtrl* addTitleCtrl_ = nullptr;
    wxTextCtrl* addSectionCtrl_ = nullptr;
    wxTextCtrl* addInstructorCtrl_ = nullptr;
    wxTextCtrl* addClassroomCtrl_ = nullptr;
    wxChoice* addSemesterChoice_ = nullptr;
    wxChoice* addDayChoice_ = nullptr;
    wxChoice* addStartChoice_ = nullptr;
    wxChoice* addEndChoice_ = nullptr;
    wxButton* addResetButton_ = nullptr;
    wxButton* addButton_ = nullptr;
    wxTextCtrl* updateCodeCtrl_ = nullptr;
    wxTextCtrl* updateSectionCtrl_ = nullptr;
    wxChoice* updateLoadSemesterChoice_ = nullptr;
    wxButton* updateLoadButton_ = nullptr;
    wxTextCtrl* updateTitleCtrl_ = nullptr;
    wxTextCtrl* updateInstructorCtrl_ = nullptr;
    wxChoice* updateDayChoice_ = nullptr;
    wxChoice* updateStartChoice_ = nullptr;
    wxChoice* updateEndChoice_ = nullptr;
    wxTextCtrl* updateClassroomCtrl_ = nullptr;
    wxChoice* updateEditSemesterChoice_ = nullptr;
    wxButton* updateCancelButton_ = nullptr;
    wxButton* updateSaveButton_ = nullptr;
    wxTextCtrl* deleteCodeCtrl_ = nullptr;
    wxTextCtrl* deleteSectionCtrl_ = nullptr;
    wxChoice* deleteSemesterChoice_ = nullptr;
    wxButton* deleteLoadButton_ = nullptr;
    wxTextCtrl* deleteInfoCodeCtrl_ = nullptr;
    wxTextCtrl* deleteInfoTitleCtrl_ = nullptr;
    wxTextCtrl* deleteInfoSectionCtrl_ = nullptr;
    wxTextCtrl* deleteInfoInstructorCtrl_ = nullptr;
    wxTextCtrl* deleteInfoDayCtrl_ = nullptr;
    wxTextCtrl* deleteInfoTimeCtrl_ = nullptr;
    wxTextCtrl* deleteInfoClassroomCtrl_ = nullptr;
    wxTextCtrl* deleteInfoSemesterCtrl_ = nullptr;
    wxStaticText* deleteWarningLabel_ = nullptr;
    wxButton* deleteCancelButton_ = nullptr;
    wxButton* deleteButton_ = nullptr;

    wxGrid* resultGrid_ = nullptr;
    CourseGridTable* resultTable_ = nullptr;
    wxTextCtrl* rawResponseCtrl_ = nullptr;
    static constexpr std::size_t kMaxRawLogLines = 160;
    std::vector<wxString> rawLogLines_;
};

class CourseGuiApp : public wxApp {
public:
    bool OnInit() override {
        WSADATA wsaData;
        const int startupResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (startupResult != 0) {
            wxMessageBox(stdToWx("WSAStartup failed: " + std::to_string(startupResult)),
                         "Startup Error",
                         wxOK | wxICON_ERROR);
            return false;
        }

        MainFrame* frame = new MainFrame();
        frame->Show(true);
        return true;
    }

    int OnExit() override {
        WSACleanup();
        return wxApp::OnExit();
    }
};

}  // namespace

wxIMPLEMENT_APP(CourseGuiApp);
