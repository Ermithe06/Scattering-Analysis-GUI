#include <wx/wx.h>
#include <wx/scrolwin.h>
#include <wx/dcbuffer.h>
#include <wx/filepicker.h>
#include <wx/dir.h>
#include <wx/filename.h>
#include <wx/listctrl.h>
#include <fstream>
#include <vector>
#include <algorithm>

using namespace std;

// ================================================================
// ImagePanel — Handles image display, zooming, selection, and editing
// ================================================================
class ImagePanel : public wxScrolledWindow
{
public:
    ImagePanel(wxWindow* parent)
        : wxScrolledWindow(parent, wxID_ANY)
    {
        // Enable smooth painting and white background
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetBackgroundColour(*wxWHITE);
        SetScrollRate(10, 10);

        // Bind event handlers for various user actions
        Bind(wxEVT_PAINT, &ImagePanel::OnPaint, this);
        Bind(wxEVT_LEFT_DOWN, &ImagePanel::OnLeftClick, this);
        Bind(wxEVT_MOTION, &ImagePanel::OnMouseMove, this);
        Bind(wxEVT_MOUSEWHEEL, &ImagePanel::OnMouseWheel, this);
        Bind(wxEVT_LEFT_DOWN, &ImagePanel::OnLeftDown, this);
        Bind(wxEVT_LEFT_UP, &ImagePanel::OnLeftUp, this);
    }

    // Set a new image and save old one to history for undo
    void SetImage(const wxImage& img)
    {
        if (m_originalImg.IsOk())
            m_history.push_back(m_originalImg);

        m_originalImg = img;
        ZoomFit(); // Automatically fit image to panel
    }

    wxImage GetOriginalImage() const { return m_originalImg; }

    // Return the currently selected rectangle (if any)
    wxRect GetSelectionRect() const { return m_selection; }

    // Zoom controls
    void ZoomIn()  { m_zoomFactor *= 1.2; m_fitMode = false; ApplyZoom(); }
    void ZoomOut() { m_zoomFactor /= 1.2; if (m_zoomFactor < 0.01) m_zoomFactor = 0.01; m_fitMode = false; ApplyZoom(); }
    void ZoomFit() { m_fitMode = true; ApplyZoom(); }

    // Copy selected region to clipboard
    void CopySelection()
    {
        if (!m_selection.IsEmpty())
            m_clipboard = m_originalImg.GetSubImage(m_selection);
    }

    // Undo last image change
    void Undo()
    {
        if (!m_history.empty())
        {
            m_originalImg = m_history.back();
            m_history.pop_back();
            ZoomFit();
        }
        else
        {
            wxMessageBox("No previous image to undo.", "Undo", wxICON_INFORMATION);
        }
    }

private:
    wxBitmap m_bitmap;             // Current bitmap to display
    wxImage m_originalImg;         // Original image data
    double m_zoomFactor = 1.0;     // Current zoom scale
    bool m_fitMode = true;         // Whether zoom is fit-to-screen
    wxRect m_selection;            // User's selection rectangle
    bool m_selecting = false;      // True while user is selecting
    wxPoint m_startPoint;          // Selection start
    wxImage m_clipboard;           // Copied image region
    vector<wxImage> m_history;     // Undo history

    // Supported blend and drawing modes
    enum BlendMode { AND, OR, XOR, BLEND };
    enum DrawMode  { NONE, TEXT, RECT, ELLIPSE, ARROW, POLYGON };
    DrawMode m_drawMode = NONE;

    // Adjusts image scaling and updates the display
    void ApplyZoom()
    {
        if (!m_originalImg.IsOk()) return;

        wxSize panelSize = GetClientSize();
        int newW, newH;

        if (m_fitMode)
        {
            // Compute best scale to fit panel dimensions
            double scaleX = (double)panelSize.x / m_originalImg.GetWidth();
            double scaleY = (double)panelSize.y / m_originalImg.GetHeight();
            m_zoomFactor = min(scaleX, scaleY);
        }

        newW = (int)(m_originalImg.GetWidth() * m_zoomFactor);
        newH = (int)(m_originalImg.GetHeight() * m_zoomFactor);

        // Scale image and update bitmap
        wxImage scaled = m_originalImg.Scale(newW, newH, wxIMAGE_QUALITY_HIGH);
        m_bitmap = wxBitmap(scaled);
        SetVirtualSize(newW, newH);
        Refresh();

        // Update zoom level text on parent frame
        wxFrame* frame = dynamic_cast<wxFrame*>(GetParent());
        if (frame)
        {
            wxString zoomStr = m_fitMode ? "Zoom: Fit" : wxString::Format("Zoom: %.0f%%", m_zoomFactor * 100);
            frame->SetStatusText(zoomStr, 1);
        }
    }

    // Paint handler: draws image, selection, and shape outlines
    void OnPaint(wxPaintEvent&)
    {
        wxAutoBufferedPaintDC dc(this);
        DoPrepareDC(dc);
        dc.Clear();

        if (m_bitmap.IsOk())
            dc.DrawBitmap(m_bitmap, 0, 0, true);

        if (!m_selection.IsEmpty())
        {
            dc.SetPen(*wxRED_PEN);
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            dc.DrawRectangle(m_selection);
        }
    }

    // Display pixel info when left-clicking
    void OnLeftClick(wxMouseEvent& event)
    {
        wxPoint pos = CalcUnscrolledPosition(event.GetPosition());
        ShowPixelInfo(pos);
    }

    // Handles selection box drawing while dragging mouse
    void OnMouseMove(wxMouseEvent& event)
    {
        wxPoint pos = CalcUnscrolledPosition(event.GetPosition());

        if (event.Dragging() && event.LeftIsDown() && m_selecting)
        {
            int x1 = min(m_startPoint.x, pos.x);
            int y1 = min(m_startPoint.y, pos.y);
            int x2 = max(m_startPoint.x, pos.x);
            int y2 = max(m_startPoint.y, pos.y);
            m_selection = wxRect(wxPoint(x1, y1), wxSize(x2 - x1, y2 - y1));
            Refresh();
        }

        ShowPixelInfo(pos);
    }

    // Zoom in/out using mouse wheel
    void OnMouseWheel(wxMouseEvent& event)
    {
        int rot = event.GetWheelRotation();
        if (rot > 0) ZoomIn(); else ZoomOut();
    }

    // Display RGB info of pixel under cursor
    void ShowPixelInfo(const wxPoint& pos)
    {
        if (!m_originalImg.IsOk()) return;
        if (pos.x < 0 || pos.y < 0 || pos.x >= m_originalImg.GetWidth() || pos.y >= m_originalImg.GetHeight()) return;

        unsigned char* data = m_originalImg.GetData();
        int idx = (pos.y * m_originalImg.GetWidth() + pos.x) * 3;
        int r = data[idx], g = data[idx + 1], b = data[idx + 2];

        wxFrame* frame = dynamic_cast<wxFrame*>(GetParent());
        if (frame)
            frame->SetStatusText(wxString::Format("X:%d Y:%d R:%d G:%d B:%d", pos.x, pos.y, r, g, b), 0);
    }

    // Start a new selection when mouse is pressed
    void OnLeftDown(wxMouseEvent& event)
    {
        m_startPoint = CalcUnscrolledPosition(event.GetPosition());
        m_selecting = true;
        CaptureMouse();
    }

    // Finalize selection when mouse is released
    void OnLeftUp(wxMouseEvent& event)
    {
        if (m_selecting)
        {
            wxPoint endPoint = CalcUnscrolledPosition(event.GetPosition());
            int x1 = min(m_startPoint.x, endPoint.x);
            int y1 = min(m_startPoint.y, endPoint.y);
            int x2 = max(m_startPoint.x, endPoint.x);
            int y2 = max(m_startPoint.y, endPoint.y);
            m_selection = wxRect(wxPoint(x1, y1), wxSize(x2 - x1, y2 - y1));
            m_selecting = false;
            ReleaseMouse();
            Refresh();
        }
    }

    // Cuts selection and replaces region with white pixels
    void CutSelection()
    {
        CopySelection();
        wxImage img = m_originalImg;
        unsigned char* data = img.GetData();
        for (int y = m_selection.y; y < m_selection.GetBottom(); ++y)
            for (int x = m_selection.x; x < m_selection.GetRight(); ++x)
            {
                int idx = (y * img.GetWidth() + x) * 3;
                data[idx] = data[idx + 1] = data[idx + 2] = 255;
            }
        SetImage(img);
    }

    // Paste clipboard contents using selected blend mode
    void PasteClipboard(wxPoint dest, BlendMode mode)
    {
        if (!m_clipboard.IsOk()) return;

        wxImage img = m_originalImg;
        unsigned char* dst = img.GetData();
        unsigned char* src = m_clipboard.GetData();
        int w = m_clipboard.GetWidth(), h = m_clipboard.GetHeight();

        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                int dx = dest.x + x, dy = dest.y + y;
                if (dx >= img.GetWidth() || dy >= img.GetHeight()) continue;

                int di = (dy * img.GetWidth() + dx) * 3;
                int si = (y * w + x) * 3;

                for (int c = 0; c < 3; ++c)
                {
                    switch (mode)
                    {
                        case AND:   dst[di + c] &= src[si + c]; break;
                        case OR:    dst[di + c] |= src[si + c]; break;
                        case XOR:   dst[di + c] ^= src[si + c]; break;
                        case BLEND: dst[di + c] = (dst[di + c] + src[si + c]) / 2; break;
                    }
                }
            }

        SetImage(img);
    }
};

// ================================================================
// ImageFrame — Window containing toolbar and image viewer
// ================================================================
class ImageFrame : public wxFrame
{
public:
    ImageFrame(wxWindow* parent, const wxString& filepath)
        : wxFrame(parent, wxID_ANY, "Image Display", wxDefaultPosition, wxSize(820, 750))
    {
        // Layout setup
        wxBoxSizer* vbox = new wxBoxSizer(wxVERTICAL);

        // --- Toolbar creation ---
        wxToolBar* toolbar = new wxToolBar(this, wxID_ANY);
        toolbar->AddTool(wxID_ZOOM_IN, "Zoom In", CreateLabeledBitmap("+"));
        toolbar->AddTool(wxID_ZOOM_OUT, "Zoom Out", CreateLabeledBitmap("-"));
        toolbar->AddTool(wxID_ZOOM_100, "Fit", CreateLabeledBitmap("Fit"));
        toolbar->AddSeparator();

        // Custom action buttons
        m_rotateId = wxNewId(); m_flipHId = wxNewId(); m_flipVId = wxNewId();
        m_cropId = wxNewId();   m_resizeId = wxNewId(); m_copyId = wxNewId(); m_undoId = wxNewId();

        toolbar->AddTool(m_rotateId, "Rotate 90°", CreateLabeledBitmap("R90"));
        toolbar->AddTool(m_flipHId, "Flip H", CreateLabeledBitmap("FH"));
        toolbar->AddTool(m_flipVId, "Flip V", CreateLabeledBitmap("FV"));
        toolbar->AddTool(m_cropId,  "Crop",    CreateLabeledBitmap("Crop"));
        toolbar->AddTool(m_resizeId,"Resize",  CreateLabeledBitmap("Size"));
        toolbar->AddTool(m_copyId,  "Copy",    CreateLabeledBitmap("Copy"));
        toolbar->AddTool(m_undoId,  "Undo",    CreateLabeledBitmap("Undo"));
        toolbar->Realize();

        vbox->Add(toolbar, 0, wxEXPAND);

        // Image display area
        m_imagePanel = new ImagePanel(this);
        vbox->Add(m_imagePanel, 1, wxEXPAND);

        SetSizer(vbox);
        CreateStatusBar(2);
        SetStatusText("Ready", 0);

        // Load image file
        LoadImage(filepath);

        // Bind toolbar events
        Bind(wxEVT_TOOL, &ImageFrame::OnZoomIn, this, wxID_ZOOM_IN);
        Bind(wxEVT_TOOL, &ImageFrame::OnZoomOut, this, wxID_ZOOM_OUT);
        Bind(wxEVT_TOOL, &ImageFrame::OnZoomFit, this, wxID_ZOOM_100);
        Bind(wxEVT_TOOL, &ImageFrame::OnRotate90, this, m_rotateId);
        Bind(wxEVT_TOOL, &ImageFrame::OnFlipH, this, m_flipHId);
        Bind(wxEVT_TOOL, &ImageFrame::OnFlipV, this, m_flipVId);
        Bind(wxEVT_TOOL, &ImageFrame::OnCrop, this, m_cropId);
        Bind(wxEVT_TOOL, &ImageFrame::OnResize, this, m_resizeId);
        Bind(wxEVT_TOOL, &ImageFrame::OnCopy, this, m_copyId);
        Bind(wxEVT_TOOL, &ImageFrame::OnUndo, this, m_undoId);
        Bind(wxEVT_SIZE, &ImageFrame::OnResizeEvent, this);

        Centre();
    }

private:
    ImagePanel* m_imagePanel; // Pointer to the image display area

    // Toolbar action IDs
    int m_rotateId, m_flipHId, m_flipVId, m_cropId, m_resizeId, m_copyId, m_undoId;

    // Helper to create simple text icons
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

    // Reads raw image data from file and converts it to grayscale
    void LoadImage(const wxString& filepath)
    {
        ifstream file(filepath.mb_str(), ios::binary);
        if (!file) return;

        // Skip header (custom binary format)
        file.seekg(3072, ios::beg);
        const int WIDTH = 2082, HEIGHT = 2217, PIXEL_DEPTH = 4;

        vector<unsigned char> buffer(WIDTH * HEIGHT * PIXEL_DEPTH);
        file.read((char*)buffer.data(), buffer.size());
        if (file.gcount() < (streamsize)buffer.size()) return;

        // Convert to grayscale image
        wxImage img(WIDTH, HEIGHT, true);
        unsigned char* rgb = img.GetData();
        for (int i = 0; i < WIDTH * HEIGHT; i++)
        {
            unsigned char r = buffer[i * 4 + 2];
            unsigned char g = buffer[i * 4 + 1];
            unsigned char b = buffer[i * 4 + 0];
            unsigned char grey = (unsigned char)(0.299 * r + 0.587 * g + 0.114 * b);
            rgb[i * 3 + 0] = grey;
            rgb[i * 3 + 1] = grey;
            rgb[i * 3 + 2] = grey;
        }
        m_imagePanel->SetImage(img);
    }

    // Toolbar event handlers
    void OnZoomIn(wxCommandEvent&)  { m_imagePanel->ZoomIn(); }
    void OnZoomOut(wxCommandEvent&) { m_imagePanel->ZoomOut(); }
    void OnZoomFit(wxCommandEvent&) { m_imagePanel->ZoomFit(); }
    void OnCopy(wxCommandEvent&)    { m_imagePanel->CopySelection(); }
    void OnUndo(wxCommandEvent&)    { m_imagePanel->Undo(); }

    // Maintain zoom fit on window resize
    void OnResizeEvent(wxSizeEvent& evt)
    {
        m_imagePanel->ZoomFit();
        evt.Skip();
    }

    // Rotate, flip, crop, resize handlers
    void OnRotate90(wxCommandEvent&) { auto img = m_imagePanel->GetOriginalImage().Rotate90(true); m_imagePanel->SetImage(img); }
    void OnFlipH(wxCommandEvent&)    { auto img = m_imagePanel->GetOriginalImage().Mirror(false); m_imagePanel->SetImage(img); }
    void OnFlipV(wxCommandEvent&)    { auto img = m_imagePanel->GetOriginalImage().Mirror(true);  m_imagePanel->SetImage(img); }

    void OnCrop(wxCommandEvent&)
    {
        wxRect rect = m_imagePanel->GetSelectionRect();
        wxImage img = m_imagePanel->GetOriginalImage();

        // Validate and crop
        if (!rect.IsEmpty() && rect.x >= 0 && rect.y >= 0 &&
            rect.GetRight() <= img.GetWidth() && rect.GetBottom() <= img.GetHeight())
            m_imagePanel->SetImage(img.GetSubImage(rect));
        else
            wxMessageBox("Invalid selection for cropping.", "Crop", wxICON_INFORMATION);
    }

    // Resize image via dialog prompt
    void OnResize(wxCommandEvent&)
    {
        wxImage img = m_imagePanel->GetOriginalImage();
        wxTextEntryDialog dlg(this, "Enter new size: width,height", "Resize Image",
            wxString::Format("%d,%d", img.GetWidth(), img.GetHeight()));

        if (dlg.ShowModal() == wxID_OK)
        {
            wxString val = dlg.GetValue();
            long w, h;
            if (val.BeforeFirst(',').ToLong(&w) && val.AfterFirst(',').ToLong(&h) && w > 0 && h > 0)
                m_imagePanel->SetImage(img.Scale(w, h, wxIMAGE_QUALITY_HIGH));
            else
                wxMessageBox("Invalid input format or dimensions. Use positive width,height", "Resize", wxICON_ERROR);
        }
    }
};

// ================================================================
// FileBrowser — Displays files/folders and opens images
// ================================================================
class FileBrowser : public wxPanel
{
public:
    FileBrowser(wxWindow* parent)
        : wxPanel(parent, wxID_ANY)
    {
        // Layout for file list and buttons
        wxBoxSizer* vbox = new wxBoxSizer(wxVERTICAL)
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
    }

private:
    wxListCtrl* m_listCtrl;
    vector<wxFileName> m_items;

    void UpdateList()
    {
        m_listCtrl->DeleteAllItems();
        for (const auto& fn : m_items)
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
            m_items.erase(m_items.begin() + sel);
            UpdateList();
        }
    }

    void OnItemActivated(wxListEvent& event)
    {
        wxFileName fn = m_items[event.GetIndex()];
        if (fn.FileExists())
        {
            auto* frame = new ImageFrame(nullptr, fn.GetFullPath());
            frame->Show();
        }
        else if (fn.DirExists())
        {
            // Open folder: show its contents
            wxDir dir(fn.GetFullPath());
            if (!dir.IsOpened()) return;

            wxString name;
            bool cont = dir.GetFirst(&name, "*", wxDIR_DIRS | wxDIR_FILES);
            vector<wxFileName> folderItems;
            while (cont)
            {
                folderItems.push_back(wxFileName(fn.GetFullPath(), name));
                cont = dir.GetNext(&name);
            }

            m_items = folderItems;
            UpdateList();
        }
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
// ================================================================
// MyApp
// ================================================================
class MyApp : public wxApp
{
public:
    bool OnInit() override
    {
        wxFrame* frame = new wxFrame(nullptr, wxID_ANY, "File Browser", wxDefaultPosition, wxSize(800, 500));
        new FileBrowser(frame);
        frame->Show();
        return true;
    }
};

wxIMPLEMENT_APP(MyApp);
