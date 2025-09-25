#include <wx/wx.h>
#include <wx/scrolwin.h>
#include <wx/dcbuffer.h>
#include <wx/dir.h>
#include <wx/filename.h>
#include <wx/listctrl.h>
#include <fstream>
#include <vector>
#include <stack>
#include <algorithm>

// ================================================================
// ImagePanel
// ================================================================
class ImagePanel : public wxScrolledWindow
{
public:
    ImagePanel(wxWindow* parent)
        : wxScrolledWindow(parent, wxID_ANY)
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetBackgroundColour(*wxWHITE);
        SetScrollRate(10, 10);
        Bind(wxEVT_PAINT, &ImagePanel::OnPaint, this);
    }

    void SetImage(const wxImage& img) { m_originalImg = img; ZoomFit(); }

    void ZoomIn() { m_zoomFactor *= 1.2; m_fitMode = false; ApplyZoom(); }
    void ZoomOut() { m_zoomFactor /= 1.2; if (m_zoomFactor < 0.01) m_zoomFactor = 0.01; m_fitMode = false; ApplyZoom(); }
    void ZoomFit() { m_fitMode = true; ApplyZoom(); }

private:
    wxBitmap m_bitmap;
    wxImage m_originalImg;
    double m_zoomFactor = 1.0;
    bool m_fitMode = true;

    void ApplyZoom()
    {
        if (!m_originalImg.IsOk()) return;

        wxSize panelSize = GetClientSize();
        int newW, newH;

        if (m_fitMode)
        {
            double scaleX = (double)panelSize.x / m_originalImg.GetWidth();
            double scaleY = (double)panelSize.y / m_originalImg.GetHeight();
            m_zoomFactor = std::min(scaleX, scaleY);
        }

        newW = (int)(m_originalImg.GetWidth() * m_zoomFactor);
        newH = (int)(m_originalImg.GetHeight() * m_zoomFactor);

        wxImage scaled = m_originalImg.Scale(newW, newH, wxIMAGE_QUALITY_HIGH);
        m_bitmap = wxBitmap(scaled);

        SetVirtualSize(newW, newH);  // Enable scrolling
        Refresh();

        if (auto frame = dynamic_cast<wxFrame*>(GetParent()))
        {
            wxString zoomStr = m_fitMode ? "Zoom: Fit" : wxString::Format("Zoom: %.0f%%", m_zoomFactor * 100);
            frame->SetStatusText(zoomStr, 1);
        }
    }

    void OnPaint(wxPaintEvent&)
    {
        wxAutoBufferedPaintDC dc(this);
        DoPrepareDC(dc);
        dc.Clear();
        if (m_bitmap.IsOk())
            dc.DrawBitmap(m_bitmap, 0, 0, true);
    }
};

// ================================================================
// ImageFrame
// ================================================================
class ImageFrame : public wxFrame
{
public:
    ImageFrame(wxWindow* parent, const wxString& filepath)
        : wxFrame(parent, wxID_ANY, "Image Display", wxDefaultPosition, wxSize(820, 750))
    {
        wxBoxSizer* vbox = new wxBoxSizer(wxVERTICAL);

        wxToolBar* toolbar = new wxToolBar(this, wxID_ANY);
        toolbar->AddTool(wxID_ZOOM_IN, "Zoom In", CreateLabeledBitmap("+"));
        toolbar->AddTool(wxID_ZOOM_OUT, "Zoom Out", CreateLabeledBitmap("-"));
        toolbar->AddTool(wxID_ZOOM_100, "Fit", CreateLabeledBitmap("Fit"));
        toolbar->Realize();
        vbox->Add(toolbar, 0, wxEXPAND);

        m_imagePanel = new ImagePanel(this);
        vbox->Add(m_imagePanel, 1, wxEXPAND);

        SetSizer(vbox);
        CreateStatusBar(2);
        SetStatusText("Ready", 0);

        LoadImage(filepath);

        Bind(wxEVT_TOOL, &ImageFrame::OnZoomIn, this, wxID_ZOOM_IN);
        Bind(wxEVT_TOOL, &ImageFrame::OnZoomOut, this, wxID_ZOOM_OUT);
        Bind(wxEVT_TOOL, &ImageFrame::OnZoomFit, this, wxID_ZOOM_100);
        Bind(wxEVT_SIZE, &ImageFrame::OnResize, this);

        Centre();
    }

private:
    ImagePanel* m_imagePanel;

    wxBitmap CreateLabeledBitmap(const wxString& label)
    {
        wxBitmap bmp(24, 24);
        wxMemoryDC dc(bmp);
        dc.SetBackground(*wxWHITE_BRUSH);
        dc.Clear();
        dc.DrawText(label, 4, 4);
        dc.SelectObject(wxNullBitmap);
        return bmp;
    }

    void LoadImage(const wxString& filepath)
    {
        wxImage img;
        if (!img.LoadFile(filepath)) return;
        m_imagePanel->SetImage(img);
    }

    void OnZoomIn(wxCommandEvent&) { m_imagePanel->ZoomIn(); }
    void OnZoomOut(wxCommandEvent&) { m_imagePanel->ZoomOut(); }
    void OnZoomFit(wxCommandEvent&) { m_imagePanel->ZoomFit(); }
    void OnResize(wxSizeEvent& evt) { m_imagePanel->ZoomFit(); evt.Skip(); }
};

// ================================================================
// FileBrowser
// ================================================================
class FileBrowser : public wxPanel
{
public:
    FileBrowser(wxWindow* parent)
        : wxPanel(parent, wxID_ANY)
    {
        wxBoxSizer* vbox = new wxBoxSizer(wxVERTICAL);

        m_backButton = new wxButton(this, wxID_ANY, "Back");
        m_backButton->Enable(false);
        vbox->Add(m_backButton, 0, wxALL | wxALIGN_LEFT, 5);

        m_listCtrl = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
            wxLC_REPORT | wxLC_SINGLE_SEL);
        m_listCtrl->InsertColumn(0, "Name", wxLIST_FORMAT_LEFT, 250);
        m_listCtrl->InsertColumn(1, "Type", wxLIST_FORMAT_LEFT, 100);
        m_listCtrl->InsertColumn(2, "Size", wxLIST_FORMAT_RIGHT, 120);
        vbox->Add(m_listCtrl, 1, wxEXPAND);

        wxBoxSizer* btnBox = new wxBoxSizer(wxHORIZONTAL);
        wxButton* addFileBtn = new wxButton(this, wxID_ANY, "Add File");
        wxButton* addFolderBtn = new wxButton(this, wxID_ANY, "Add Folder");
        wxButton* delBtn = new wxButton(this, wxID_ANY, "Delete Selected");
        btnBox->Add(addFileBtn, 0, wxALL, 5);
        btnBox->Add(addFolderBtn, 0, wxALL, 5);
        btnBox->Add(delBtn, 0, wxALL, 5);
        vbox->Add(btnBox, 0, wxALIGN_LEFT);

        SetSizer(vbox);

        addFileBtn->Bind(wxEVT_BUTTON, &FileBrowser::OnAddFile, this);
        addFolderBtn->Bind(wxEVT_BUTTON, &FileBrowser::OnAddFolder, this);
        delBtn->Bind(wxEVT_BUTTON, &FileBrowser::OnDeleteSelected, this);
        m_listCtrl->Bind(wxEVT_LIST_ITEM_ACTIVATED, &FileBrowser::OnItemActivated, this);
        m_backButton->Bind(wxEVT_BUTTON, &FileBrowser::OnBack, this);
    }

private:
    wxListCtrl* m_listCtrl;
    wxButton* m_backButton;
    std::vector<wxFileName> m_items;
    std::stack<wxFileName> m_navStack;
    wxFileName m_currentFolder;

    void UpdateList()
    {
        m_listCtrl->DeleteAllItems();

        if (!m_currentFolder.IsOk())
        {
            for (const auto& fn : m_items) AddItemToList(fn);
        }
        else
        {
            wxDir dir(m_currentFolder.GetFullPath());
            if (!dir.IsOpened()) return;
            wxString name;
            bool cont = dir.GetFirst(&name, "*", wxDIR_DIRS | wxDIR_FILES);
            while (cont)
            {
                wxFileName fn(m_currentFolder.GetFullPath(), name);
                AddItemToList(fn);
                cont = dir.GetNext(&name);
            }
        }
    }

    void AddItemToList(const wxFileName& fn)
    {
        long idx = m_listCtrl->InsertItem(m_listCtrl->GetItemCount(), fn.GetFullName());
        if (fn.DirExists())
        {
            m_listCtrl->SetItem(idx, 1, "Folder");
            m_listCtrl->SetItem(idx, 2, FormatSize(GetFolderSize(fn)));
        }
        else if (fn.FileExists())
        {
            wxString ext = fn.GetExt();
            if (ext.IsEmpty()) ext = "File";
            m_listCtrl->SetItem(idx, 1, ext);
            m_listCtrl->SetItem(idx, 2, FormatSize(fn.GetSize()));
        }
    }

    void OnAddFile(wxCommandEvent&)
    {
        wxFileDialog dlg(this, "Select files", "", "", "*.*", wxFD_OPEN | wxFD_FILE_MUST_EXIST | wxFD_MULTIPLE);
        if (dlg.ShowModal() != wxID_OK) return;
        wxArrayString paths;
        dlg.GetPaths(paths);
        for (const auto& p : paths) m_items.push_back(wxFileName(p));
        UpdateList();
    }

    void OnAddFolder(wxCommandEvent&)
    {
        wxDirDialog dlg(this, "Select folder");
        if (dlg.ShowModal() != wxID_OK) return;
        m_items.push_back(wxFileName(dlg.GetPath()));
        UpdateList();
    }

    void OnDeleteSelected(wxCommandEvent&)
    {
        long sel = m_listCtrl->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
        if (sel != -1)
        {
            if (!m_currentFolder.IsOk())
                m_items.erase(m_items.begin() + sel);
            else
            {
                wxMessageBox("Cannot delete inside navigated folder.");
                return;
            }
            UpdateList();
        }
    }

    void FileBrowser::OnItemActivated(wxListEvent& event)
    {
        wxString itemText = m_listCtrl->GetItemText(event.GetIndex());
        wxFileName fn(m_currentFolder.GetFullPath(), itemText);

        if (wxDirExists(fn.GetFullPath()))
        {
            // Navigate into folder
            m_navStack.push(m_currentFolder);
            m_currentFolder = fn;
            m_backButton->Enable(true);
            UpdateList();
        }
        else if (wxFileExists(fn.GetFullPath()))
        {
            // Open any file in the ImageFrame
            auto* frame = new ImageFrame(nullptr, fn.GetFullPath());
            if (!frame->IsShownOnScreen())  // fallback check
            {
                wxMessageBox("This file could not be opened in the image viewer.", "Error");
                frame->Destroy();
            }
            else
            {
                frame->Show();
            }
        }
    }

    void OnBack(wxCommandEvent&)
    {
        if (!m_navStack.empty())
        {
            m_currentFolder = m_navStack.top();
            m_navStack.pop();
        }
        else
            m_currentFolder.Clear();

        m_backButton->Enable(!m_navStack.empty() || m_currentFolder.IsOk());
        UpdateList();
    }

    static wxULongLong GetFolderSize(const wxFileName& folder)
    {
        wxULongLong total = 0;
        wxDir dir(folder.GetFullPath());
        if (!dir.IsOpened()) return total;
        wxString name;
        bool cont = dir.GetFirst(&name, "*", wxDIR_DIRS | wxDIR_FILES);
        while (cont)
        {
            wxFileName fn(folder.GetFullPath(), name);
            if (fn.DirExists()) total += GetFolderSize(fn);
            else total += fn.GetSize();
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
};

// ================================================================
// MyApp
// ================================================================
class MyApp : public wxApp
{
public:
    bool OnInit() override
    {
        wxFrame* frame = new wxFrame(nullptr, wxID_ANY, "File Browser", wxDefaultPosition, wxSize(800, 600));
        new FileBrowser(frame);
        frame->Show();
        return true;
    }
};

wxIMPLEMENT_APP(MyApp);
