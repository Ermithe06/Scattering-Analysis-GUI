#include <wx/wx.h>
#include <wx/scrolwin.h>
#include <wx/dcbuffer.h>
#include <wx/filepicker.h>
#include <wx/dir.h>
#include <wx/filename.h>
#include <wx/listctrl.h>
#include <fstream>
#include <vector>
#include <thread>
#include <mutex>

using namespace std;

// Constants
static const int HEADER_OFFSET = 3072;
static const int WIDTH = 2082;
static const int HEIGHT = 2217;
static const int PIXEL_DEPTH = 4;

// Helper: Create labeled bitmap for toolbar
wxBitmap CreateLabeledBitmap(const wxString& label, int w = 32, int h = 32)
{
    wxBitmap bmp(w, h);
    wxMemoryDC dc(bmp);
    dc.SetBackground(*wxWHITE_BRUSH);
    dc.Clear();
    dc.SetTextForeground(*wxBLACK);
    wxFont font(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD);
    dc.SetFont(font);
    wxSize ts = dc.GetTextExtent(label);
    dc.DrawText(label, (w - ts.x) / 2, (h - ts.y) / 2);
    dc.SelectObject(wxNullBitmap);
    return bmp;
}

// ImagePanel
class ImagePanel : public wxScrolledWindow
{
public:
    ImagePanel(wxWindow* parent) : wxScrolledWindow(parent, wxID_ANY)
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetBackgroundColour(*wxWHITE);
        SetScrollRate(10, 10);
        SetCursor(wxCursor(wxCURSOR_ARROW));
        Bind(wxEVT_PAINT, &ImagePanel::OnPaint, this);
    }

    void SetBitmap(const wxBitmap& bmp)
    {
        m_bitmap = bmp;
        if (m_bitmap.IsOk())
        {
            SetVirtualSize(m_bitmap.GetWidth(), m_bitmap.GetHeight());
            Refresh();
        }
    }

    void SetHandScrollingEnabled(bool enable)
    {
        SetCursor(enable ? wxCursor(wxCURSOR_HAND) : wxCursor(wxCURSOR_ARROW));
    }

private:
    wxBitmap m_bitmap;

    void OnPaint(wxPaintEvent&)
    {
        wxAutoBufferedPaintDC dc(this);
        DoPrepareDC(dc);
        dc.Clear();
        if (m_bitmap.IsOk())
            dc.DrawBitmap(m_bitmap, 0, 0, true);
    }
};

// ImageFrame
class ImageFrame : public wxFrame
{
public:
    ImageFrame(wxWindow* parent, const wxString& filepath)
        : wxFrame(parent, wxID_ANY, "Image Viewer", wxDefaultPosition, wxSize(820, 750))
    {
        wxBoxSizer* vbox = new wxBoxSizer(wxVERTICAL);
        m_imagepanel = new ImagePanel(this);
        vbox->Add(m_imagepanel, 1, wxEXPAND);

        // Toolbar
        wxToolBar* toolbar = CreateToolBar();
        toolbar->AddTool(wxID_ZOOM_IN, "Zoom In", CreateLabeledBitmap("+"));
        toolbar->AddTool(wxID_ZOOM_OUT, "Zoom Out", CreateLabeledBitmap("-"));
        toolbar->AddTool(wxID_ZOOM_100, "Fit", CreateLabeledBitmap("Fit"));
        toolbar->Realize();

        CreateStatusBar(2);
        SetStatusText("Ready", 0);

        SetSizer(vbox);

        LoadImage(filepath);

        // Bind events
        Bind(wxEVT_TOOL, [this](wxCommandEvent&) { OnZoomIn(); }, wxID_ZOOM_IN);
        Bind(wxEVT_TOOL, [this](wxCommandEvent&) { OnZoomOut(); }, wxID_ZOOM_OUT);
        Bind(wxEVT_TOOL, [this](wxCommandEvent&) { OnZoomFit(); }, wxID_ZOOM_100);
        Bind(wxEVT_SIZE, &ImageFrame::OnResize, this);
        Bind(wxEVT_CHAR_HOOK, &ImageFrame::OnKeyPress, this);

        Centre();
    }

private:
    ImagePanel* m_imagepanel;
    wxImage m_originalImg;
    wxBitmap m_bitmap;
    double m_zoomFactor = 1.0;
    bool m_fitMode = true;

    void LoadImage(const wxString& filepath)
    {
        ifstream file(filepath.mb_str().data(), ios::binary);
        if (!file) return;
        file.seekg(HEADER_OFFSET, ios::beg);
        vector<unsigned char> buffer(WIDTH * HEIGHT * PIXEL_DEPTH);
        file.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
        if (file.gcount() < (streamsize)buffer.size()) return;

        wxImage img(WIDTH, HEIGHT, true);
        unsigned char* rgb = img.GetData();
        for (int i = 0; i < WIDTH * HEIGHT; i++)
        {
            unsigned char r = buffer[i * 4 + 2];
            unsigned char g = buffer[i * 4 + 1];
            unsigned char b = buffer[i * 4 + 0];
            unsigned char grey = static_cast<unsigned char>(0.299 * r + 0.587 * g + 0.114 * b);
            rgb[i * 3 + 0] = grey; rgb[i * 3 + 1] = grey; rgb[i * 3 + 2] = grey;
        }
        m_originalImg = img;
        FitImage();
    }

    void ApplyZoom()
    {
        if (!m_originalImg.IsOk()) return;
        wxSize panelSize = m_imagepanel->GetClientSize();

        if (m_fitMode)
        {
            double scaleX = (double)panelSize.x / m_originalImg.GetWidth();
            double scaleY = (double)panelSize.y / m_originalImg.GetHeight();
            m_zoomFactor = std::min(scaleX, scaleY);
        }

        int newW = static_cast<int>(m_originalImg.GetWidth() * m_zoomFactor);
        int newH = static_cast<int>(m_originalImg.GetHeight() * m_zoomFactor);

        wxImage scaled = m_originalImg.Scale(newW, newH, wxIMAGE_QUALITY_HIGH);
        m_bitmap = wxBitmap(scaled);

        // Center image inside panel
        wxBitmap centered(panelSize.x, panelSize.y);
        {
            wxMemoryDC dc(centered);
            dc.SetBackground(*wxWHITE_BRUSH);
            dc.Clear();
            int x = (panelSize.x - newW) / 2;
            int y = (panelSize.y - newH) / 2;
            dc.DrawBitmap(m_bitmap, std::max(0, x), std::max(0, y), true);
        }
        m_imagepanel->SetBitmap(centered);

        wxString zoomStr = m_fitMode ? "Zoom: Fit" : wxString::Format("Zoom: %.0f%%", m_zoomFactor * 100);
        SetStatusText(zoomStr, 1);

        m_imagepanel->SetHandScrollingEnabled(newW > panelSize.x || newH > panelSize.y);
    }

    void OnZoomIn() { m_zoomFactor *= 1.2; m_fitMode = false; ApplyZoom(); }
    void OnZoomOut() { m_zoomFactor /= 1.2; if (m_zoomFactor < 0.01)m_zoomFactor = 0.01; m_fitMode = false; ApplyZoom(); }
    void OnZoomFit() { FitImage(); }
    void FitImage() { m_fitMode = true; ApplyZoom(); }

    void OnResize(wxSizeEvent& event) { if (m_fitMode) ApplyZoom(); event.Skip(); }

    void OnKeyPress(wxKeyEvent& event)
    {
        if (event.ControlDown())
        {
            switch (event.GetKeyCode())
            {
            case WXK_ADD: case '=': OnZoomIn(); return;
            case WXK_SUBTRACT: case '-': OnZoomOut(); return;
            case '0': OnZoomFit(); return;
            }
        }
        event.Skip();
    }
};

// FileBrowser
class FileBrowser : public wxPanel
{
public:
    FileBrowser(wxWindow* parent)
        : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(500, 300))
    {
        wxBoxSizer* vbox = new wxBoxSizer(wxVERTICAL);

        // Folder selection
        wxBoxSizer* hbox = new wxBoxSizer(wxHORIZONTAL);
        wxStaticText* label = new wxStaticText(this, wxID_ANY, "Folder:");
        hbox->Add(label, 0, wxALL, 5);
        m_dirPicker = new wxDirPickerCtrl(this, wxID_ANY);
        hbox->Add(m_dirPicker, 1, wxALL | wxEXPAND, 5);
        vbox->Add(hbox, 0, wxEXPAND);

        // List control
        m_listCtrl = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
            wxLC_REPORT | wxLC_SINGLE_SEL);
        m_listCtrl->InsertColumn(0, "Name", wxLIST_FORMAT_LEFT, 200);
        m_listCtrl->InsertColumn(1, "Type", wxLIST_FORMAT_LEFT, 80);
        m_listCtrl->InsertColumn(2, "Size", wxLIST_FORMAT_RIGHT, 80);
        vbox->Add(m_listCtrl, 1, wxALL | wxEXPAND, 5);

        // Add/Delete buttons
        wxBoxSizer* btnBox = new wxBoxSizer(wxHORIZONTAL);
        wxButton* addBtn = new wxButton(this, wxID_ANY, "Add Folder");
        wxButton* delBtn = new wxButton(this, wxID_ANY, "Delete Folder");
        btnBox->Add(addBtn, 0, wxALL, 5);
        btnBox->Add(delBtn, 0, wxALL, 5);
        vbox->Add(btnBox, 0, wxALIGN_LEFT);

        SetSizer(vbox);

        // Bind events
        m_dirPicker->Bind(wxEVT_DIRPICKER_CHANGED, &FileBrowser::OnDirChanged, this);
        m_listCtrl->Bind(wxEVT_LIST_ITEM_ACTIVATED, &FileBrowser::OnFileActivated, this);
        addBtn->Bind(wxEVT_BUTTON, &FileBrowser::OnAddFolder, this);
        delBtn->Bind(wxEVT_BUTTON, &FileBrowser::OnDeleteFolder, this);
    }

private:
    wxDirPickerCtrl* m_dirPicker;
    wxListCtrl* m_listCtrl;

    wxULongLong GetFolderSize(const wxFileName& folder)
    {
        wxULongLong total = 0;
        wxDir dir(folder.GetFullPath());
        if (!dir.IsOpened()) return total;

        wxString name;
        bool cont = dir.GetFirst(&name, "*", wxDIR_DIRS | wxDIR_FILES);
        while (cont)
        {
            wxFileName fn(folder.GetFullPath(), name);
            if (fn.DirExists())
                total += GetFolderSize(fn);
            else
                total += fn.GetSize();
            cont = dir.GetNext(&name);
        }
        return total;
    }

    static wxString FormatSize(wxULongLong size)
    {
        double sz = static_cast<double>(size.GetValue());
        const char* units[] = { "B", "KB", "MB", "GB", "TB" };
        int u = 0;
        while (sz >= 1024.0 && u < 4) { sz /= 1024.0; u++; }
        return wxString::Format("%.2f %s", sz, units[u]);
    }

    void RefreshFolderList(const wxString& folder)
    {
        m_listCtrl->DeleteAllItems();
        wxDir dir(folder);
        if (!dir.IsOpened()) return;

        wxString name;
        bool cont = dir.GetFirst(&name, "*", wxDIR_DIRS | wxDIR_FILES);
        while (cont)
        {
            wxFileName fn(folder, name);
            long index = m_listCtrl->InsertItem(m_listCtrl->GetItemCount(), fn.GetFullName());
            if (fn.DirExists())
            {
                m_listCtrl->SetItem(index, 1, "Folder");
                wxULongLong sz = GetFolderSize(fn);
                m_listCtrl->SetItem(index, 2, FormatSize(sz));
            }
            else
            {
                m_listCtrl->SetItem(index, 1, "File");
                m_listCtrl->SetItem(index, 2, FormatSize(fn.GetSize()));
            }
            cont = dir.GetNext(&name);
        }
    }

    void OnDirChanged(wxFileDirPickerEvent& event)
    {
        RefreshFolderList(event.GetPath());
    }

    void OnFileActivated(wxListEvent& event)
    {
        wxString parent = m_dirPicker->GetPath();
        wxString name = m_listCtrl->GetItemText(event.GetIndex());
        wxFileName fn(parent, name);
        if (fn.FileExists())
        {
            auto* frame = new ImageFrame(nullptr, fn.GetFullPath());
            frame->Show();
        }
        else if (fn.DirExists())
        {
            m_dirPicker->SetPath(fn.GetFullPath());
            RefreshFolderList(fn.GetFullPath());
        }
    }

    void OnAddFolder(wxCommandEvent&)
    {
        wxString parent = m_dirPicker->GetPath();
        if (parent.IsEmpty()) return;
        wxTextEntryDialog dlg(this, "Enter new folder name:", "Add Folder");
        if (dlg.ShowModal() != wxID_OK) return;
        wxString newName = dlg.GetValue();
        wxFileName newFolder(parent, newName);
        if (newFolder.DirExists()) { wxMessageBox("Folder exists!"); return; }
        if (wxFileName::Mkdir(newFolder.GetFullPath(), wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL))
            RefreshFolderList(parent);
        else wxMessageBox("Failed to create folder.");
    }

    void OnDeleteFolder(wxCommandEvent&)
    {
        wxString parent = m_dirPicker->GetPath();
        long sel = m_listCtrl->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
        if (sel == -1) { wxMessageBox("Select a folder to delete."); return; }
        wxString name = m_listCtrl->GetItemText(sel);
        wxFileName folder(parent, name);
        if (!folder.DirExists()) { wxMessageBox("Not a folder."); return; }
        if (wxMessageBox("Delete " + folder.GetFullPath() + "?", "Confirm", wxYES_NO | wxICON_QUESTION) != wxYES) return;
        if (wxFileName::Rmdir(folder.GetFullPath(), wxPATH_RMDIR_RECURSIVE))
            RefreshFolderList(parent);
        else wxMessageBox("Failed to delete folder.");
    }
};

// MyApp
class MyApp : public wxApp
{
public:
    bool OnInit() override
    {
        wxFrame* frame = new wxFrame(nullptr, wxID_ANY, "File Browser", wxDefaultPosition, wxSize(600, 400));
        new FileBrowser(frame);
        frame->Show();
        return true;
    }
};

wxIMPLEMENT_APP(MyApp);
