#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>

#include "Protocol.h"

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/event.h>
#include <wx/frame.h>
#include <wx/grid.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/notebook.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/wx.h>

#include <atomic>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
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

std::string wxToStd(const wxString& value) {
    return std::string(value.mb_str());
}

wxString stdToWx(const std::string& value) {
    return wxString::FromUTF8(value.c_str());
}

std::string readFixedField(const std::string& line, std::size_t begin, std::size_t width) {
    if (line.size() <= begin) {
        return "";
    }
    return Protocol::trim(line.substr(begin, width));
}

std::vector<CourseRow> parseCourseRows(const ServerResponse& response) {
    std::vector<CourseRow> rows;
    if (!Protocol::startsWithIgnoreCase(response.firstLine, "RESULT ")) {
        return rows;
    }

    for (const std::string& line : response.bodyLines) {
        if (line.empty() ||
            line.find("---") == 0 ||
            Protocol::startsWithIgnoreCase(line, "CourseCode") ||
            Protocol::startsWithIgnoreCase(line, "No matching courses.")) {
            continue;
        }

        CourseRow row;
        row.code = readFixedField(line, 0, 12);
        row.title = readFixedField(line, 12, 30);
        row.section = readFixedField(line, 42, 9);
        row.instructor = readFixedField(line, 51, 20);
        row.day = readFixedField(line, 71, 8);
        row.start = readFixedField(line, 79, 10);
        row.end = readFixedField(line, 89, 10);
        row.classroom = readFixedField(line, 99, 12);
        row.semester = readFixedField(line, 111, 10);

        if (!row.code.empty()) {
            rows.push_back(row);
        }
    }

    return rows;
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

    ServerResponse connectTo(const std::string& host, int port) {
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
        response.ok = true;
        response.connected = true;
        response.firstLine = greeting;
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
    }

    mutable std::mutex mutex_;
    SOCKET socket_ = INVALID_SOCKET;
};

class MainFrame : public wxFrame {
public:
    MainFrame()
        : wxFrame(nullptr,
                  wxID_ANY,
                  "Course Timetable GUI Client",
                  wxDefaultPosition,
                  wxSize(1180, 720)) {
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
        wxBoxSizer* rootSizer = new wxBoxSizer(wxVERTICAL);

        rootSizer->Add(BuildConnectionBar(root), 0, wxEXPAND | wxALL, 10);

        wxBoxSizer* bodySizer = new wxBoxSizer(wxHORIZONTAL);

        notebook_ = new wxNotebook(root, wxID_ANY);
        notebook_->AddPage(BuildQueryPage(notebook_), "Query");
        notebook_->AddPage(BuildAdminPage(notebook_), "Admin");
        notebook_->AddPage(BuildRawPage(notebook_), "Raw");

        bodySizer->Add(notebook_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
        bodySizer->Add(BuildResultPanel(root), 1, wxEXPAND | wxRIGHT | wxBOTTOM, 10);

        rootSizer->Add(bodySizer, 1, wxEXPAND);
        root->SetSizer(rootSizer);

        CreateStatusBar();
        SetStatusText("Disconnected");
    }

    wxSizer* BuildConnectionBar(wxWindow* parent) {
        wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);

        sizer->Add(new wxStaticText(parent, wxID_ANY, "Host"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        hostCtrl_ = new wxTextCtrl(parent, wxID_ANY, "127.0.0.1", wxDefaultPosition, wxSize(150, -1));
        sizer->Add(hostCtrl_, 0, wxRIGHT, 12);

        sizer->Add(new wxStaticText(parent, wxID_ANY, "Port"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        portCtrl_ = new wxSpinCtrl(parent, wxID_ANY);
        portCtrl_->SetRange(1, 65535);
        portCtrl_->SetValue(Protocol::DEFAULT_PORT);
        sizer->Add(portCtrl_, 0, wxRIGHT, 12);

        connectButton_ = new wxButton(parent, wxID_ANY, "Connect");
        disconnectButton_ = new wxButton(parent, wxID_ANY, "Disconnect");
        sizer->Add(connectButton_, 0, wxRIGHT, 6);
        sizer->Add(disconnectButton_, 0, wxRIGHT, 16);

        connectionLabel_ = new wxStaticText(parent, wxID_ANY, "Disconnected");
        sizer->Add(connectionLabel_, 0, wxALIGN_CENTER_VERTICAL);
        sizer->AddStretchSpacer(1);

        loginStateLabel_ = new wxStaticText(parent, wxID_ANY, "Role: Student");
        sizer->Add(loginStateLabel_, 0, wxALIGN_CENTER_VERTICAL);

        return sizer;
    }

    wxWindow* BuildQueryPage(wxWindow* parent) {
        wxPanel* panel = new wxPanel(parent);
        wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

        wxFlexGridSizer* grid = new wxFlexGridSizer(4, 3, 8, 8);
        grid->AddGrowableCol(1, 1);

        codeCtrl_ = new wxTextCtrl(panel, wxID_ANY);
        instructorCtrl_ = new wxTextCtrl(panel, wxID_ANY);
        semesterCtrl_ = new wxTextCtrl(panel, wxID_ANY, "2026S");

        queryCodeButton_ = new wxButton(panel, wxID_ANY, "Search");
        queryInstructorButton_ = new wxButton(panel, wxID_ANY, "Search");
        querySemesterButton_ = new wxButton(panel, wxID_ANY, "Search");

        grid->Add(new wxStaticText(panel, wxID_ANY, "Course Code"), 0, wxALIGN_CENTER_VERTICAL);
        grid->Add(codeCtrl_, 1, wxEXPAND);
        grid->Add(queryCodeButton_, 0);

        grid->Add(new wxStaticText(panel, wxID_ANY, "Instructor"), 0, wxALIGN_CENTER_VERTICAL);
        grid->Add(instructorCtrl_, 1, wxEXPAND);
        grid->Add(queryInstructorButton_, 0);

        grid->Add(new wxStaticText(panel, wxID_ANY, "Semester"), 0, wxALIGN_CENTER_VERTICAL);
        grid->Add(semesterCtrl_, 1, wxEXPAND);
        grid->Add(querySemesterButton_, 0);

        wxBoxSizer* timeSizer = new wxBoxSizer(wxHORIZONTAL);
        dayChoice_ = new wxChoice(panel, wxID_ANY);
        dayChoice_->Append("Mon");
        dayChoice_->Append("Tue");
        dayChoice_->Append("Wed");
        dayChoice_->Append("Thu");
        dayChoice_->Append("Fri");
        dayChoice_->SetSelection(0);
        startCtrl_ = new wxTextCtrl(panel, wxID_ANY, "09:00", wxDefaultPosition, wxSize(72, -1));
        endCtrl_ = new wxTextCtrl(panel, wxID_ANY, "12:00", wxDefaultPosition, wxSize(72, -1));
        timeSizer->Add(dayChoice_, 0, wxRIGHT, 6);
        timeSizer->Add(startCtrl_, 0, wxRIGHT, 6);
        timeSizer->Add(endCtrl_, 0);

        queryTimeButton_ = new wxButton(panel, wxID_ANY, "Search");
        grid->Add(new wxStaticText(panel, wxID_ANY, "Time Slot"), 0, wxALIGN_CENTER_VERTICAL);
        grid->Add(timeSizer, 1, wxEXPAND);
        grid->Add(queryTimeButton_, 0);

        sizer->Add(grid, 0, wxEXPAND | wxALL, 12);
        sizer->AddStretchSpacer(1);
        panel->SetSizer(sizer);
        return panel;
    }

    wxWindow* BuildAdminPage(wxWindow* parent) {
        wxPanel* panel = new wxPanel(parent);
        wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

        wxBoxSizer* loginSizer = new wxBoxSizer(wxHORIZONTAL);
        adminUserCtrl_ = new wxTextCtrl(panel, wxID_ANY, "admin", wxDefaultPosition, wxSize(110, -1));
        adminPasswordCtrl_ = new wxTextCtrl(panel, wxID_ANY, "", wxDefaultPosition, wxSize(110, -1), wxTE_PASSWORD);
        loginButton_ = new wxButton(panel, wxID_ANY, "Login");

        loginSizer->Add(new wxStaticText(panel, wxID_ANY, "User"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        loginSizer->Add(adminUserCtrl_, 0, wxRIGHT, 8);
        loginSizer->Add(new wxStaticText(panel, wxID_ANY, "Password"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        loginSizer->Add(adminPasswordCtrl_, 0, wxRIGHT, 8);
        loginSizer->Add(loginButton_, 0);
        sizer->Add(loginSizer, 0, wxEXPAND | wxALL, 12);
        sizer->Add(new wxStaticLine(panel), 0, wxEXPAND | wxLEFT | wxRIGHT, 12);

        wxFlexGridSizer* addGrid = new wxFlexGridSizer(9, 2, 7, 8);
        addGrid->AddGrowableCol(1, 1);

        addCodeCtrl_ = AddLabeledText(panel, addGrid, "CourseCode");
        addTitleCtrl_ = AddLabeledText(panel, addGrid, "CourseTitle");
        addSectionCtrl_ = AddLabeledText(panel, addGrid, "Section");
        addInstructorCtrl_ = AddLabeledText(panel, addGrid, "Instructor");
        addDayCtrl_ = AddLabeledText(panel, addGrid, "Day");
        addStartCtrl_ = AddLabeledText(panel, addGrid, "StartTime");
        addEndCtrl_ = AddLabeledText(panel, addGrid, "EndTime");
        addClassroomCtrl_ = AddLabeledText(panel, addGrid, "Classroom");
        addSemesterCtrl_ = AddLabeledText(panel, addGrid, "Semester");

        addButton_ = new wxButton(panel, wxID_ANY, "Add Course");
        sizer->Add(addGrid, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);
        sizer->Add(addButton_, 0, wxALIGN_RIGHT | wxALL, 12);
        sizer->Add(new wxStaticLine(panel), 0, wxEXPAND | wxLEFT | wxRIGHT, 12);

        wxFlexGridSizer* updateGrid = new wxFlexGridSizer(4, 2, 7, 8);
        updateGrid->AddGrowableCol(1, 1);
        updateCodeCtrl_ = AddLabeledText(panel, updateGrid, "CourseCode");
        updateSectionCtrl_ = AddLabeledText(panel, updateGrid, "Section");
        updateGrid->Add(new wxStaticText(panel, wxID_ANY, "Field"), 0, wxALIGN_CENTER_VERTICAL);
        updateFieldChoice_ = new wxChoice(panel, wxID_ANY);
        const char* fields[] = {
            "CourseTitle", "Instructor", "Day", "StartTime",
            "EndTime", "Time", "Classroom", "Semester"
        };
        for (const char* field : fields) {
            updateFieldChoice_->Append(field);
        }
        updateFieldChoice_->SetSelection(0);
        updateGrid->Add(updateFieldChoice_, 1, wxEXPAND);
        updateValueCtrl_ = AddLabeledText(panel, updateGrid, "New Value");

        updateButton_ = new wxButton(panel, wxID_ANY, "Update Course");
        sizer->Add(updateGrid, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);
        sizer->Add(updateButton_, 0, wxALIGN_RIGHT | wxALL, 12);

        wxBoxSizer* deleteSizer = new wxBoxSizer(wxHORIZONTAL);
        deleteCodeCtrl_ = new wxTextCtrl(panel, wxID_ANY, "", wxDefaultPosition, wxSize(120, -1));
        deleteSectionCtrl_ = new wxTextCtrl(panel, wxID_ANY, "", wxDefaultPosition, wxSize(80, -1));
        deleteButton_ = new wxButton(panel, wxID_ANY, "Delete");
        deleteSizer->Add(new wxStaticText(panel, wxID_ANY, "CourseCode"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        deleteSizer->Add(deleteCodeCtrl_, 0, wxRIGHT, 8);
        deleteSizer->Add(new wxStaticText(panel, wxID_ANY, "Section"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        deleteSizer->Add(deleteSectionCtrl_, 0, wxRIGHT, 8);
        deleteSizer->Add(deleteButton_, 0);
        sizer->Add(deleteSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);

        panel->SetSizer(sizer);
        return panel;
    }

    wxWindow* BuildRawPage(wxWindow* parent) {
        wxPanel* panel = new wxPanel(parent);
        wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

        rawCommandCtrl_ = new wxTextCtrl(panel, wxID_ANY, "HELP");
        rawSendButton_ = new wxButton(panel, wxID_ANY, "Send");

        wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
        row->Add(rawCommandCtrl_, 1, wxRIGHT, 8);
        row->Add(rawSendButton_, 0);
        sizer->Add(row, 0, wxEXPAND | wxALL, 12);
        sizer->AddStretchSpacer(1);

        panel->SetSizer(sizer);
        return panel;
    }

    wxWindow* BuildResultPanel(wxWindow* parent) {
        wxPanel* panel = new wxPanel(parent);
        wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

        resultGrid_ = new wxGrid(panel, wxID_ANY);
        resultGrid_->CreateGrid(0, 9);
        const char* labels[] = {
            "Code", "Title", "Section", "Instructor", "Day",
            "Start", "End", "Classroom", "Semester"
        };
        for (int col = 0; col < 9; ++col) {
            resultGrid_->SetColLabelValue(col, labels[col]);
        }
        resultGrid_->EnableEditing(false);
        resultGrid_->SetRowLabelSize(42);
        resultGrid_->SetColSize(0, 92);
        resultGrid_->SetColSize(1, 240);
        resultGrid_->SetColSize(2, 74);
        resultGrid_->SetColSize(3, 160);
        resultGrid_->SetColSize(4, 70);
        resultGrid_->SetColSize(5, 78);
        resultGrid_->SetColSize(6, 78);
        resultGrid_->SetColSize(7, 100);
        resultGrid_->SetColSize(8, 90);

        rawResponseCtrl_ = new wxTextCtrl(panel,
                                          wxID_ANY,
                                          "",
                                          wxDefaultPosition,
                                          wxSize(-1, 150),
                                          wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2);

        sizer->Add(resultGrid_, 1, wxEXPAND);
        sizer->Add(rawResponseCtrl_, 0, wxEXPAND | wxTOP, 8);
        panel->SetSizer(sizer);
        return panel;
    }

    wxTextCtrl* AddLabeledText(wxWindow* parent, wxFlexGridSizer* sizer, const wxString& label) {
        wxTextCtrl* ctrl = new wxTextCtrl(parent, wxID_ANY);
        sizer->Add(new wxStaticText(parent, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
        sizer->Add(ctrl, 1, wxEXPAND);
        return ctrl;
    }

    void BindEvents() {
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { Close(); }, wxID_EXIT);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { ShowProtocolHelp(); }, wxID_HELP);
        Bind(EVT_COMMAND_FINISHED, &MainFrame::OnCommandFinished, this);

        connectButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ConnectAsync(); });
        disconnectButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Disconnect(); });

        queryCodeButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            SendCommandFromInput("QUERY_CODE", codeCtrl_);
        });
        queryInstructorButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            SendCommandFromInput("QUERY_INSTRUCTOR", instructorCtrl_);
        });
        querySemesterButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            SendCommandFromInput("QUERY_SEMESTER", semesterCtrl_);
        });
        queryTimeButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            const std::string day = wxToStd(dayChoice_->GetStringSelection());
            const std::string start = wxToStd(startCtrl_->GetValue());
            const std::string end = wxToStd(endCtrl_->GetValue());
            if (RequireText(startCtrl_, "Start time") && RequireText(endCtrl_, "End time")) {
                RunCommandAsync("QUERY_TIME " + day + " " + start + " " + end);
            }
        });

        loginButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            if (RequireText(adminUserCtrl_, "Username") &&
                RequireText(adminPasswordCtrl_, "Password")) {
                RunCommandAsync("LOGIN " + wxToStd(adminUserCtrl_->GetValue()) + " " +
                                wxToStd(adminPasswordCtrl_->GetValue()));
            }
        });

        addButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            const wxTextCtrl* fields[] = {
                addCodeCtrl_, addTitleCtrl_, addSectionCtrl_, addInstructorCtrl_,
                addDayCtrl_, addStartCtrl_, addEndCtrl_, addClassroomCtrl_, addSemesterCtrl_
            };
            const char* names[] = {
                "CourseCode", "CourseTitle", "Section", "Instructor", "Day",
                "StartTime", "EndTime", "Classroom", "Semester"
            };
            for (int i = 0; i < 9; ++i) {
                if (wxToStd(fields[i]->GetValue()).empty()) {
                    wxMessageBox(stdToWx(std::string(names[i]) + " cannot be empty."),
                                 "Missing Value",
                                 wxOK | wxICON_WARNING,
                                 this);
                    return;
                }
            }

            RunCommandAsync("ADD " + wxToStd(addCodeCtrl_->GetValue()) + "|" +
                            wxToStd(addTitleCtrl_->GetValue()) + "|" +
                            wxToStd(addSectionCtrl_->GetValue()) + "|" +
                            wxToStd(addInstructorCtrl_->GetValue()) + "|" +
                            wxToStd(addDayCtrl_->GetValue()) + "|" +
                            wxToStd(addStartCtrl_->GetValue()) + "|" +
                            wxToStd(addEndCtrl_->GetValue()) + "|" +
                            wxToStd(addClassroomCtrl_->GetValue()) + "|" +
                            wxToStd(addSemesterCtrl_->GetValue()));
        });

        updateButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            if (RequireText(updateCodeCtrl_, "CourseCode") &&
                RequireText(updateSectionCtrl_, "Section") &&
                RequireText(updateValueCtrl_, "New value")) {
                RunCommandAsync("UPDATE " + wxToStd(updateCodeCtrl_->GetValue()) + " " +
                                wxToStd(updateSectionCtrl_->GetValue()) + " " +
                                wxToStd(updateFieldChoice_->GetStringSelection()) + " " +
                                wxToStd(updateValueCtrl_->GetValue()));
            }
        });

        deleteButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            if (RequireText(deleteCodeCtrl_, "CourseCode") &&
                RequireText(deleteSectionCtrl_, "Section")) {
                RunCommandAsync("DELETE " + wxToStd(deleteCodeCtrl_->GetValue()) + " " +
                                wxToStd(deleteSectionCtrl_->GetValue()));
            }
        });

        rawSendButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            if (RequireText(rawCommandCtrl_, "Command")) {
                RunCommandAsync(wxToStd(rawCommandCtrl_->GetValue()));
            }
        });
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

    void SendCommandFromInput(const std::string& command, wxTextCtrl* ctrl) {
        if (RequireText(ctrl, command)) {
            RunCommandAsync(command + " " + wxToStd(ctrl->GetValue()));
        }
    }

    void ConnectAsync() {
        if (busy_) {
            return;
        }

        const std::string host = wxToStd(hostCtrl_->GetValue());
        const int port = portCtrl_->GetValue();

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

        worker_ = std::thread([this, host, port]() {
            ServerResponse response = client_.connectTo(host, port);
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

        std::ostringstream payload;
        payload << (response.ok ? "1" : "0") << '\n'
                << response.error << '\n'
                << response.firstLine << '\n';
        for (const std::string& line : response.bodyLines) {
            payload << line << '\n';
        }
        event->SetPayload<std::string>(payload.str());
        wxQueueEvent(this, event);
    }

    void OnCommandFinished(wxThreadEvent& event) {
        busy_ = false;
        if (worker_.joinable()) {
            worker_.join();
        }

        const ServerResponse response = UnpackResponse(event.GetPayload<std::string>());
        const std::string command = wxToStd(event.GetString());
        const bool connected = event.GetInt() == 1 && client_.isConnected();

        UpdateConnectionState(connected);
        SetControlsEnabled(true);

        if (!response.ok) {
            rawResponseCtrl_->SetValue(stdToWx(response.error));
            SetStatusText(stdToWx(response.error));
            return;
        }

        rawResponseCtrl_->SetValue(stdToWx(response.rawText()));
        SetStatusText(stdToWx(response.firstLine));

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
            PopulateGrid(parseCourseRows(response));
        }

        if (Protocol::startsWithIgnoreCase(command, "ADD ") ||
            Protocol::startsWithIgnoreCase(command, "UPDATE ") ||
            Protocol::startsWithIgnoreCase(command, "DELETE ")) {
            wxMessageBox(stdToWx(response.firstLine), "Server Response", wxOK | wxICON_INFORMATION, this);
        }
    }

    ServerResponse UnpackResponse(const std::string& payload) {
        ServerResponse response;
        std::istringstream input(payload);
        std::string okLine;
        std::getline(input, okLine);
        response.ok = okLine == "1";
        std::getline(input, response.error);
        std::getline(input, response.firstLine);

        std::string line;
        while (std::getline(input, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (!line.empty()) {
                response.bodyLines.push_back(line);
            }
        }
        return response;
    }

    void Disconnect() {
        client_.disconnect();
        isAdmin_ = false;
        UpdateConnectionState(false);
        UpdateAdminState();
        SetStatusText("Disconnected");
    }

    void UpdateConnectionState(bool connected) {
        connectionLabel_->SetLabel(connected ? "Connected" : "Disconnected");
        connectButton_->Enable(!connected && !busy_);
        disconnectButton_->Enable(connected && !busy_);
        hostCtrl_->Enable(!connected && !busy_);
        portCtrl_->Enable(!connected && !busy_);
        UpdateAdminState();
    }

    void UpdateAdminState() {
        loginStateLabel_->SetLabel(isAdmin_ ? "Role: Administrator" : "Role: Student");

        const bool adminEnabled = isAdmin_ && client_.isConnected() && !busy_;
        addButton_->Enable(adminEnabled);
        updateButton_->Enable(adminEnabled);
        deleteButton_->Enable(adminEnabled);
    }

    void SetControlsEnabled(bool enabled) {
        const bool connected = client_.isConnected();
        connectButton_->Enable(enabled && !connected);
        disconnectButton_->Enable(enabled && connected);
        hostCtrl_->Enable(enabled && !connected);
        portCtrl_->Enable(enabled && !connected);

        queryCodeButton_->Enable(enabled && connected);
        queryInstructorButton_->Enable(enabled && connected);
        querySemesterButton_->Enable(enabled && connected);
        queryTimeButton_->Enable(enabled && connected);
        loginButton_->Enable(enabled && connected);
        rawSendButton_->Enable(enabled && connected);
        UpdateAdminState();
    }

    void PopulateGrid(const std::vector<CourseRow>& rows) {
        const int existingRows = resultGrid_->GetNumberRows();
        if (existingRows > 0) {
            resultGrid_->DeleteRows(0, existingRows);
        }

        if (rows.empty()) {
            return;
        }

        resultGrid_->AppendRows(static_cast<int>(rows.size()));
        for (std::size_t rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
            const CourseRow& row = rows[rowIndex];
            const int rowNumber = static_cast<int>(rowIndex);
            resultGrid_->SetCellValue(rowNumber, 0, stdToWx(row.code));
            resultGrid_->SetCellValue(rowNumber, 1, stdToWx(row.title));
            resultGrid_->SetCellValue(rowNumber, 2, stdToWx(row.section));
            resultGrid_->SetCellValue(rowNumber, 3, stdToWx(row.instructor));
            resultGrid_->SetCellValue(rowNumber, 4, stdToWx(row.day));
            resultGrid_->SetCellValue(rowNumber, 5, stdToWx(row.start));
            resultGrid_->SetCellValue(rowNumber, 6, stdToWx(row.end));
            resultGrid_->SetCellValue(rowNumber, 7, stdToWx(row.classroom));
            resultGrid_->SetCellValue(rowNumber, 8, stdToWx(row.semester));
        }
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

    wxNotebook* notebook_ = nullptr;
    wxTextCtrl* hostCtrl_ = nullptr;
    wxSpinCtrl* portCtrl_ = nullptr;
    wxButton* connectButton_ = nullptr;
    wxButton* disconnectButton_ = nullptr;
    wxStaticText* connectionLabel_ = nullptr;
    wxStaticText* loginStateLabel_ = nullptr;

    wxTextCtrl* codeCtrl_ = nullptr;
    wxTextCtrl* instructorCtrl_ = nullptr;
    wxTextCtrl* semesterCtrl_ = nullptr;
    wxChoice* dayChoice_ = nullptr;
    wxTextCtrl* startCtrl_ = nullptr;
    wxTextCtrl* endCtrl_ = nullptr;
    wxButton* queryCodeButton_ = nullptr;
    wxButton* queryInstructorButton_ = nullptr;
    wxButton* querySemesterButton_ = nullptr;
    wxButton* queryTimeButton_ = nullptr;

    wxTextCtrl* adminUserCtrl_ = nullptr;
    wxTextCtrl* adminPasswordCtrl_ = nullptr;
    wxButton* loginButton_ = nullptr;
    wxTextCtrl* addCodeCtrl_ = nullptr;
    wxTextCtrl* addTitleCtrl_ = nullptr;
    wxTextCtrl* addSectionCtrl_ = nullptr;
    wxTextCtrl* addInstructorCtrl_ = nullptr;
    wxTextCtrl* addDayCtrl_ = nullptr;
    wxTextCtrl* addStartCtrl_ = nullptr;
    wxTextCtrl* addEndCtrl_ = nullptr;
    wxTextCtrl* addClassroomCtrl_ = nullptr;
    wxTextCtrl* addSemesterCtrl_ = nullptr;
    wxButton* addButton_ = nullptr;
    wxTextCtrl* updateCodeCtrl_ = nullptr;
    wxTextCtrl* updateSectionCtrl_ = nullptr;
    wxChoice* updateFieldChoice_ = nullptr;
    wxTextCtrl* updateValueCtrl_ = nullptr;
    wxButton* updateButton_ = nullptr;
    wxTextCtrl* deleteCodeCtrl_ = nullptr;
    wxTextCtrl* deleteSectionCtrl_ = nullptr;
    wxButton* deleteButton_ = nullptr;

    wxTextCtrl* rawCommandCtrl_ = nullptr;
    wxButton* rawSendButton_ = nullptr;

    wxGrid* resultGrid_ = nullptr;
    wxTextCtrl* rawResponseCtrl_ = nullptr;
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
