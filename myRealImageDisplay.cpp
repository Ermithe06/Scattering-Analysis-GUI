// cleaned_image_viewer.cpp
#include <wx/wx.h>
#include <wx/scrolwin.h>
#include <wx/dcbuffer.h>
#include <wx/filepicker.h>
#include <wx/dir.h>
#include <wx/filename.h>
#include <wx/listctrl.h>
#include <wx/dynlib.h>
#include <fstream>
#include <vector>
#include <algorithm>

using namespace std;

// --------------------
// Helper / small classes (define before usage)
// --------------------
class ROIManager {
public:
    void AddROI(const wxRect& roi) { m_rois.push_back(roi); }
    void Clear() { m_rois.clear(); }
    const vector<wxRect>& GetROIs() const { return m_rois; }
private:
    vector<wxRect> m_rois;
};

class ResultsFrame : public wxFrame {
public:
    ResultsFrame(wxWindow* parent)
        : wxFrame(parent, wxID_ANY, "Results", wxDefaultPosition, wxSize(400, 300)) {
        m_textCtrl = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
            wxTE_MULTILINE | wxTE_READONLY);
        wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(m_textCtrl, 1, wxEXPAND | wxALL, 10);
        SetSizer(sizer);
    }

    void AddResult(const wxString& result) {
        m_textCtrl->AppendText(result + "\n");
    }

private:
    wxTextCtrl* m_textCtrl;
};

class HistogramFrame : public wxFrame {
public:
    HistogramFrame(wxWindow* parent, const wxImage& img)
        : wxFrame(parent, wxID_ANY, "Histogram", wxDefaultPosition, wxSize(300, 150)) {
        if (!img.IsOk()) {
            new wxStaticText(this, wxID_ANY, "No image", wxDefaultPosition);
            return;
        }

        vector<int> hist(256, 0);
        unsigned char* data = img.GetData();
        int w = img.GetWidth(), h = img.GetHeight();
        for (int i = 0; i < w * h; ++i) hist[data[i * 3]]++;

        int maxVal = *max_element(hist.begin(), hist.end());
        wxImage histImg(256, 100);
        histImg.InitAlpha(); // ensure default values
        for (int x = 0; x < 256; ++x) {
            int barHeight = maxVal > 0 ? (int)(hist[x] * 100.0 / maxVal) : 0;
            for (int y = 99; y >= 100 - barHeight; --y)
                histImg.SetRGB(x, y, 0, 0, 0);
        }

        wxStaticBitmap* bmp = new wxStaticBitmap(this, wxID_ANY, wxBitmap(histImg));
        wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(bmp, 1, wxEXPAND | wxALL, 10);
        SetSizer(sizer);
    }
};

class PluginLoader {
public:
    static bool LoadPlugin(const wxString& path, wxImage& img) {
        wxDynamicLibrary lib(path);
        if (!lib.IsLoaded()) return false;
        typedef void (*ApplyFilterFn)(wxImage&);
        ApplyFilterFn func = (ApplyFilterFn)lib.GetSymbol("ApplyFilter");
        if (!func) return false;
        func(img);
        return true;
    }
};

class StackViewer : public wxFrame {
public:
    StackViewer(wxWindow* parent, const std::vector<wxImage>& slices)
        : wxFrame(parent, wxID_ANY, "Stack Viewer", wxDefaultPosition, wxSize(800, 600)),
        m_slices(slices), m_index(0) {
        wxBoxSizer* vbox = new wxBoxSizer(wxVERTICAL);

        if (m_slices.empty()) {
            vbox->Add(new wxStaticText(this, wxID_ANY, "No slices to display"), 1, wxALL | wxALIGN_CENTER, 10);
            SetSizer(vbox);
            return;
        }

        m_bitmap = new wxStaticBitmap(this, wxID_ANY, wxBitmap(m_slices[0]));
        m_slider = new wxSlider(this, wxID_ANY, 0, 0, (int)m_slices.size() - 1);

        vbox->Add(m_bitmap, 1, wxEXPAND);
        vbox->Add(m_slider, 0, wxEXPAND | wxALL, 5);
        SetSizer(vbox);

        m_slider->Bind(wxEVT_SLIDER, &StackViewer::OnSlide, this);
    }

private:
    std::vector<wxImage> m_slices;
    int m_index = 0;
    wxStaticBitmap* m_bitmap = nullptr;
    wxSlider* m_slider = nullptr;

    void OnSlide(wxCommandEvent& /*evt*/) {
        if (m_slices.empty() || !m_bitmap) return;
        m_index = m_slider->GetValue();
        if (m_index >= 0 && m_index < (int)m_slices.size())
            m_bitmap->SetBitmap(wxBitmap(m_slices[m_index]));
    }
};

// --------------------
// ImagePanel
// --------------------
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
        Bind(wxEVT_MENU, &ImagePanel::OnCopy, this, wxID_COPY);
        Bind(wxEVT_MENU, &ImagePanel::OnPaste, this, wxID_PASTE);
        Bind(wxEVT_MENU, &ImagePanel::OnSave, this, wxID_SAVE);
        Bind(wxEVT_MENU, &ImagePanel::OnUndo, this, wxID_UNDO);
        Bind(wxEVT_CONTEXT_MENU, &ImagePanel::OnContextMenu, this);
        Bind(wxEVT_LEFT_DOWN, &ImagePanel::OnLeftDown, this);
        Bind(wxEVT_MOTION, &ImagePanel::OnMouseMove, this);
        Bind(wxEVT_MOUSEWHEEL, &ImagePanel::OnMouseWheel, this);
        Bind(wxEVT_LEFT_UP, &ImagePanel::OnLeftUp, this);
        Bind(wxEVT_CHAR_HOOK, &ImagePanel::OnKeyDown, this);
    }

    void OnCopy(wxCommandEvent&) { CopySelection(); }
    void OnPaste(wxCommandEvent&) { PasteClipboard(wxPoint(10, 10), BLEND); } // Default paste location
    void OnUndo(wxCommandEvent&) { Undo(); }

    void OnSave(wxCommandEvent&)
    {
        wxFileDialog dlg(this, "Save Image", "", "", "PNG files (*.png)|*.png", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
        if (dlg.ShowModal() == wxID_OK)
        {
            m_originalImg.SaveFile(dlg.GetPath(), wxBITMAP_TYPE_PNG);
        }
    }

    void SetImage(const wxImage& img)
    {
        if (m_originalImg.IsOk())
            m_history.push_back(m_originalImg);  // Save current image before replacing

        m_originalImg = img;
        ZoomFit();
    }

    ROIManager m_roiManager;
    wxImage GetOriginalImage() const { return m_originalImg; }

    wxRect GetSelectionRect() const { return m_selection; }

    void ZoomIn() { m_zoomFactor *= 1.2; m_fitMode = false; ApplyZoom(); }
    void ZoomOut() { m_zoomFactor /= 1.2; if (m_zoomFactor < 0.01) m_zoomFactor = 0.01; m_fitMode = false; ApplyZoom(); }
    void ZoomFit() { m_fitMode = true; ApplyZoom(); }
    void CopySelection()
    {
        if (!m_selection.IsEmpty() && m_originalImg.IsOk())
            m_clipboard = m_originalImg.GetSubImage(m_selection);
    }

    void OnKeyDown(wxKeyEvent& event)
    {
        if (event.ControlDown())
        {
            switch (event.GetKeyCode())
            {
            case 'C': CopySelection(); break;
            case 'V': PasteClipboard(wxPoint(10, 10), BLEND); break;
            case 'Z': Undo(); break;
            default: event.Skip(); return;
            }
        }
        else
        {
            event.Skip();
        }
    }

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
    wxBitmap m_bitmap;
    wxImage m_originalImg;
    double m_zoomFactor = 1.0;
    bool m_fitMode = true;
    wxRect m_selection;
    bool m_selecting = false;
    wxPoint m_startPoint;
    wxImage m_clipboard;

    enum BlendMode { AND, OR, XOR, BLEND };
    enum DrawMode { NONE, TEXT, RECT, ELLIPSE, ARROW, POLYGON };
    DrawMode m_drawMode = NONE;
    vector<wxImage> m_history;

    void ApplyZoom()
    {
        if (!m_originalImg.IsOk()) return;

        wxSize panelSize = GetClientSize();
        int newW = (int)(m_originalImg.GetWidth() * m_zoomFactor);
        int newH = (int)(m_originalImg.GetHeight() * m_zoomFactor);

        if (m_fitMode)
        {
            double scaleX = (double)panelSize.x / m_originalImg.GetWidth();
            double scaleY = (double)panelSize.y / m_originalImg.GetHeight();
            m_zoomFactor = min(scaleX, scaleY);
            newW = (int)(m_originalImg.GetWidth() * m_zoomFactor);
            newH = (int)(m_originalImg.GetHeight() * m_zoomFactor);
        }

        wxImage scaled = m_originalImg.Scale(newW, newH, wxIMAGE_QUALITY_HIGH);
        m_bitmap = wxBitmap(scaled);

        SetVirtualSize(newW, newH);
        Refresh();

        wxFrame* frame = dynamic_cast<wxFrame*>(GetParent());
        if (frame)
        {
            wxString zoomStr = m_fitMode ? wxString("Zoom: Fit") : wxString::Format("Zoom: %.0f%%", m_zoomFactor * 100);
            frame->SetStatusText(zoomStr, 1);
        }
    }

    void OnContextMenu(wxContextMenuEvent& /*event*/)
    {
        wxMenu menu;
        menu.Append(wxID_COPY, "Copy Selection");
        menu.Append(wxID_PASTE, "Paste Clipboard");
        menu.Append(wxID_SAVE, "Save Image As...");
        menu.AppendSeparator();
        menu.Append(wxID_UNDO, "Undo");

        PopupMenu(&menu, ScreenToClient(wxGetMousePosition()));
    }

    void OnPaint(wxPaintEvent& /*event*/)
    {
        wxAutoBufferedPaintDC dc(this);
        DoPrepareDC(dc);
        dc.Clear();

        if (m_bitmap.IsOk())
            dc.DrawBitmap(m_bitmap, 0, 0, true);

        for (const auto& roi : m_roiManager.GetROIs()) {
            dc.SetPen(*wxGREEN_PEN);
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            dc.DrawRectangle(roi);
        }

        if (!m_selection.IsEmpty())
        {
            dc.SetPen(*wxRED_PEN);
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            dc.DrawRectangle(m_selection);
        }

        if (m_drawMode == RECT && !m_selection.IsEmpty())
        {
            dc.SetPen(*wxBLUE_PEN);
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            dc.DrawRectangle(m_selection);
        }
    }

    void OnLeftDown(wxMouseEvent& event)
    {
        m_startPoint = CalcUnscrolledPosition(event.GetPosition());
        m_selecting = true;
        CaptureMouse();
    }

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
            if (!m_selection.IsEmpty())
                m_roiManager.AddROI(m_selection);
        }
    }

    void OnMouseMove(wxMouseEvent& event)
    {
        wxPoint pos = CalcUnscrolledPosition(event.GetPosition());

        if (event.Dragging() && event.LeftIsDown())
        {
            if (m_selecting)
            {
                int x1 = min(m_startPoint.x, pos.x);
                int y1 = min(m_startPoint.y, pos.y);
                int x2 = max(m_startPoint.x, pos.x);
                int y2 = max(m_startPoint.y, pos.y);
                m_selection = wxRect(wxPoint(x1, y1), wxSize(x2 - x1, y2 - y1));
                Refresh();
            }
        }

        ShowPixelInfo(pos);
    }

    void OnMouseWheel(wxMouseEvent& event)
    {
        int rot = event.GetWheelRotation();
        if (rot > 0) ZoomIn();
        else ZoomOut();
    }

    void ShowPixelInfo(const wxPoint& pos)
    {
        if (!m_originalImg.IsOk()) return;
        if (pos.x < 0 || pos.y < 0 || pos.x >= m_originalImg.GetWidth() || pos.y >= m_originalImg.GetHeight()) return;

        unsigned char* data = m_originalImg.GetData();
        int idx = (pos.y * m_originalImg.GetWidth() + pos.x) * 3;
        int r = data[idx];
        int g = data[idx + 1];
        int b = data[idx + 2];

        wxFrame* frame = dynamic_cast<wxFrame*>(GetParent());
        if (frame)
        {
            frame->SetStatusText(wxString::Format("X: %d Y: %d R: %d G: %d B: %d", pos.x, pos.y, r, g, b), 0);
        }
    }

    void CutSelection()
    {
        CopySelection();
        if (!m_originalImg.IsOk() || m_selection.IsEmpty()) return;

        wxImage img = m_originalImg;
        unsigned char* data = img.GetData();
        for (int y = m_selection.y; y < m_selection.GetBottom(); ++y)
        {
            for (int x = m_selection.x; x < m_selection.GetRight(); ++x)
            {
                int idx = (y * img.GetWidth() + x) * 3;
                data[idx] = data[idx + 1] = data[idx + 2] = 255;
            }
        }
        SetImage(img);
    }

    void PasteClipboard(wxPoint dest, BlendMode mode)
    {
        if (!m_clipboard.IsOk() || !m_originalImg.IsOk()) return;

        wxImage img = m_originalImg;
        unsigned char* dst = img.GetData();
        unsigned char* src = m_clipboard.GetData();
        int w = m_clipboard.GetWidth(), h = m_clipboard.GetHeight();

        for (int y = 0; y < h; ++y)
        {
            for (int x = 0; x < w; ++x)
            {
                int dx = dest.x + x, dy = dest.y + y;
                if (dx >= img.GetWidth() || dy >= img.GetHeight() || dx < 0 || dy < 0) continue;

                int di = (dy * img.GetWidth() + dx) * 3;
                int si = (y * w + x) * 3;

                for (int c = 0; c < 3; ++c)
                {
                    switch (mode)
                    {
                    case AND: dst[di + c] &= src[si + c]; break;
                    case OR:  dst[di + c] |= src[si + c]; break;
                    case XOR: dst[di + c] ^= src[si + c]; break;
                    case BLEND: dst[di + c] = (dst[di + c] + src[si + c]) / 2; break;
                    }
                }
            }
        }
        SetImage(img);
    }
};

// --------------------
// ImageFrame
// --------------------
class ImageFrame : public wxFrame
{
public:
    ImageFrame(wxWindow* parent, const wxString& filepath)
        : wxFrame(parent, wxID_ANY, "Image Display", wxDefaultPosition, wxSize(820, 750))
    {
        wxBoxSizer* vbox = new wxBoxSizer(wxVERTICAL);

        // create panel first
        m_imagePanel = new ImagePanel(this);

        // toolbar
        wxToolBar* toolbar = new wxToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTB_HORIZONTAL | wxNO_BORDER);
        toolbar->AddTool(wxID_ZOOM_IN, "Zoom In", CreateLabeledBitmap("+"));
        toolbar->AddTool(wxID_ZOOM_OUT, "Zoom Out", CreateLabeledBitmap("-"));
        toolbar->AddTool(wxID_ZOOM_100, "Fit", CreateLabeledBitmap("Fit"));
        toolbar->AddSeparator();

        m_rotateId = wxNewId();
        m_helpId = wxNewId();
        m_flipHId = wxNewId();
        m_flipVId = wxNewId();
        m_cropId = wxNewId();
        m_resizeId = wxNewId();
        m_copyId = wxNewId();
        m_undoId = wxNewId();

        toolbar->AddTool(m_rotateId, "Rotate 90°", CreateLabeledBitmap("R90"));
        toolbar->AddTool(m_flipHId, "Flip H", CreateLabeledBitmap("FH"));
        toolbar->AddTool(m_flipVId, "Flip V", CreateLabeledBitmap("FV"));
        toolbar->AddTool(m_cropId, "Crop", CreateLabeledBitmap("Crop"));
        toolbar->AddTool(m_resizeId, "Resize", CreateLabeledBitmap("Size"));
        toolbar->AddTool(m_copyId, "Copy", CreateLabeledBitmap("Copy"));
        toolbar->AddTool(m_undoId, "Undo", CreateLabeledBitmap("Undo"));
        toolbar->AddTool(m_helpId, "Help", CreateLabeledBitmap("?"));
        toolbar->Realize();

        vbox->Add(toolbar, 0, wxEXPAND);
        vbox->Add(m_imagePanel, 1, wxEXPAND);

        // results frame (non-modal)
        m_resultsFrame = new ResultsFrame(this);
        m_resultsFrame->Show();

        SetSizer(vbox);
        CreateStatusBar(2);
        SetStatusText("Ready", 0);

        // safe: load the image from file into wxImage then set
        LoadImage(filepath);

        // plugin optional dialog: allow user to pick plugin AFTER image loaded
        wxFileDialog pdlg(this, "Select Plugin", "", "", "*.dll;*.so", wxFD_OPEN);
        if (pdlg.ShowModal() == wxID_OK) {
            wxImage img = m_imagePanel->GetOriginalImage();
            if (img.IsOk() && PluginLoader::LoadPlugin(pdlg.GetPath(), img)) {
                m_imagePanel->SetImage(img);
            }
            else {
                wxMessageBox("Failed to load/apply plugin", "Plugin", wxICON_WARNING);
            }
        }

        // show histogram and (example) stack viewer safely
        if (m_imagePanel->GetOriginalImage().IsOk())
        {
            auto* hist = new HistogramFrame(this, m_imagePanel->GetOriginalImage());
            hist->Show();
        }

        Bind(wxEVT_TOOL, &ImageFrame::OnZoomIn, this, wxID_ZOOM_IN);
        Bind(wxEVT_TOOL, &ImageFrame::OnZoomOut, this, wxID_ZOOM_OUT);
        Bind(wxEVT_TOOL, &ImageFrame::OnZoomFit, this, wxID_ZOOM_100);
        Bind(wxEVT_TOOL, &ImageFrame::OnRotate90, this, m_rotateId);
        Bind(wxEVT_TOOL, &ImageFrame::OnFlipH, this, m_flipHId);
        Bind(wxEVT_TOOL, &ImageFrame::OnFlipV, this, m_flipVId);
        Bind(wxEVT_TOOL, &ImageFrame::OnCrop, this, m_cropId);
        Bind(wxEVT_TOOL, &ImageFrame::OnResize, this, m_resizeId);
        Bind(wxEVT_SIZE, &ImageFrame::OnResizeEvent, this);
        Bind(wxEVT_TOOL, &ImageFrame::OnCopy, this, m_copyId);
        Bind(wxEVT_TOOL, &ImageFrame::OnUndo, this, m_undoId);
        Bind(wxEVT_TOOL, &ImageFrame::OnHelp, this, m_helpId);

        Centre();
    }

private:
    ImagePanel* m_imagePanel = nullptr;
    ResultsFrame* m_resultsFrame = nullptr;

    int m_rotateId;
    int m_flipHId;
    int m_flipVId;
    int m_cropId;
    int m_resizeId;
    int m_copyId;
    int m_undoId;
    int m_helpId;

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
        // simple loader: try to load standard image formats using wxImage
        wxImage img;
        if (!img.LoadFile(filepath)) {
            // If you need special binary loading like before (header offset), implement here and
            // convert into wxImage then call m_imagePanel->SetImage(img)
            wxMessageBox("Failed to open image with wxImage loader. Provide a valid image file.", "Load Image", wxICON_WARNING);
            return;
        }
        // convert to greyscale if you want (example):
        // img = img.ConvertToGreyscale(); // optional
        m_imagePanel->SetImage(img);
        if (m_resultsFrame) {
            m_resultsFrame->AddResult("Width: " + wxString::Format("%d", img.GetWidth()));
        }
    }

    void OnZoomIn(wxCommandEvent&) { m_imagePanel->ZoomIn(); }
    void OnZoomOut(wxCommandEvent&) { m_imagePanel->ZoomOut(); }
    void OnZoomFit(wxCommandEvent&) { m_imagePanel->ZoomFit(); }
    void OnCopy(wxCommandEvent&) { m_imagePanel->CopySelection(); }
    void OnResizeEvent(wxSizeEvent& evt) { m_imagePanel->ZoomFit(); evt.Skip(); }

    void OnRotate90(wxCommandEvent&)
    {
        wxImage img = m_imagePanel->GetOriginalImage();
        if (img.IsOk()) {
            img = img.Rotate90(true);
            m_imagePanel->SetImage(img);
        }
    }
    void OnFlipH(wxCommandEvent&)
    {
        wxImage img = m_imagePanel->GetOriginalImage();
        if (img.IsOk()) {
            img = img.Mirror(false);
            m_imagePanel->SetImage(img);
        }
    }
    void OnFlipV(wxCommandEvent&)
    {
        wxImage img = m_imagePanel->GetOriginalImage();
        if (img.IsOk()) {
            img = img.Mirror(true);
            m_imagePanel->SetImage(img);
        }
    }

    void OnCrop(wxCommandEvent&)
    {
        wxRect rect = m_imagePanel->GetSelectionRect();
        wxImage img = m_imagePanel->GetOriginalImage();

        if (!rect.IsEmpty() &&
            img.IsOk() &&
            rect.x >= 0 && rect.y >= 0 &&
            rect.GetRight() <= img.GetWidth() &&
            rect.GetBottom() <= img.GetHeight())
        {
            wxImage cropped = img.GetSubImage(rect);
            m_imagePanel->SetImage(cropped);
        }
        else
        {
            wxMessageBox("Invalid selection for cropping.", "Crop", wxICON_INFORMATION);
        }
    }

    void OnUndo(wxCommandEvent&) { m_imagePanel->Undo(); }

    void OnResize(wxCommandEvent&)
    {
        wxImage img = m_imagePanel->GetOriginalImage();
        if (!img.IsOk()) return;

        wxTextEntryDialog dlg(this, "Enter new size: width,height", "Resize Image", wxString::Format("%d,%d", img.GetWidth(), img.GetHeight()));
        if (dlg.ShowModal() == wxID_OK)
        {
            wxString val = dlg.GetValue();
            wxString wStr = val.BeforeFirst(',');
            wxString hStr = val.AfterFirst(',');

            long w, h;
            if (wStr.ToLong(&w) && hStr.ToLong(&h) && w > 0 && h > 0)
            {
                wxImage resized = img.Scale((int)w, (int)h, wxIMAGE_QUALITY_HIGH);
                m_imagePanel->SetImage(resized);
            }
            else
            {
                wxMessageBox("Invalid input format or dimensions. Use positive width,height", "Resize", wxICON_ERROR);
            }
        }
    }

    void OnHelp(wxCommandEvent&)
    {
        wxString helpText =
            "Toolbar Button Guide:\n\n"
            "+ : Zoom In\n"
            "- : Zoom Out\n"
            "Fit : Fit image to window\n"
            "R90 : Rotate image 90° clockwise\n"
            "FH : Flip image horizontally\n"
            "FV : Flip image vertically\n"
            "Crop : Crop selected region\n"
            "Size : Resize image\n"
            "Copy : Copy selected region\n"
            "Undo : Revert to previous image\n"
            "?\t: Show this help dialog\n\n"
            "Mouse Interaction Guide:\n\n"
            "• Left-click on image: Start selection / Show pixel info\n"
            "• Click and drag: Select a rectangular region\n"
            "• Scroll wheel: Zoom in/out\n"
            "• Release mouse after dragging: Finalize selection\n\n"
            "Tip: You can crop, copy, or cut the selected region using toolbar buttons.";

        wxMessageBox(helpText, "Help", wxOK | wxICON_INFORMATION, this);
    }
};

// --------------------
// FileBrowser + MyApp (unchanged except small safety)
// --------------------
class FileBrowser : public wxPanel
{
public:
    FileBrowser(wxWindow* parent)
        : wxPanel(parent, wxID_ANY)
    {
        wxBoxSizer* vbox = new wxBoxSizer(wxVERTICAL);

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
