#include <wx/wx.h>            // Core wxWidgets GUI components
#include <wx/scrolwin.h>       // Scrollable window
#include <wx/dcbuffer.h>       // Buffered drawing
#include <wx/filepicker.h>     // File dialogs
#include <wx/dir.h>            // Directory handling
#include <wx/filename.h>       // File path utilities
#include <wx/listctrl.h>       // List control (grid-like view)
#include <wx/dynlib.h>         // Dynamic library loading (plugins)
#include <fstream>             // File I/O
#include <vector>              // Dynamic arrays
#include <algorithm>           // Algorithms like max_element
#include <limits>              // Numeric limits
#include <unordered_set>
#include <cmath>

using namespace std;

// Configuration constants
static constexpr double PI = 3.14159265358979323846;
static const size_t MAX_HISTORY = 16;        // Maximum number of undo steps
static const long HEADER_OFFSET = 3072;      // Legacy image file header offset

struct RadialAvgPoint {
    int R;
    double avg;
    int samples; // unique pixels counted (optional but useful)
};

// Class to manage Regions of Interest (ROIs)
class ROIManager {
public:
    void AddROI(const wxRect& roi) { m_rois.push_back(roi); } // Add ROI
    void Clear() { m_rois.clear(); }                          // Clear all ROIs
    const vector<wxRect>& GetROIs() const { return m_rois; }  // Get list of ROIs
private:
    vector<wxRect> m_rois;  // Stores rectangles for ROIs
};

// Frame to display textual results
class ResultsFrame : public wxFrame {
public:
    ResultsFrame(wxWindow* parent)
        : wxFrame(parent, wxID_ANY, "Results", wxDefaultPosition, wxSize(400, 300)) {
        // Multiline, read-only text control
        m_textCtrl = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
            wxTE_MULTILINE | wxTE_READONLY);

        // Layout manager
        wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(m_textCtrl, 1, wxEXPAND | wxALL, 10);
        SetSizer(sizer);
    }

    void AddResult(const wxString& result) {
        if (m_textCtrl) m_textCtrl->AppendText(result + "\n"); // Append line of text
    }

private:
    wxTextCtrl* m_textCtrl{ nullptr }; // Text control to show results
};

// Class to load image plugins dynamically
class PluginLoader {
public:
    static bool LoadPlugin(const wxString& path, wxImage& img) {
        if (path.IsEmpty()) return false;

        wxDynamicLibrary* lib = new wxDynamicLibrary(path); // Load library
        if (!lib->IsLoaded()) {
            delete lib;
            return false;
        }

        typedef void (*ApplyFilterFn)(wxImage&); // Function type for plugin
        ApplyFilterFn func = reinterpret_cast<ApplyFilterFn>(lib->GetSymbol("ApplyFilter"));
        if (!func) {
            wxMessageBox("ApplyFilter() not found in plugin!", "Plugin Error", wxICON_ERROR);
            lib->Unload();
            delete lib;
            return false;
        }

        try
        {
            func(img);
        }
        catch (...)
        {
            wxMessageBox("Plugin crashed while applying filter.", "Plugin Error", wxICON_ERROR);
            lib->Unload();
            delete lib;
            return false;
        }

        m_libs.push_back(lib);
        return true;
    }
private:
    static vector<wxDynamicLibrary*> m_libs; // Track loaded plugins
};

vector<wxDynamicLibrary*> PluginLoader::m_libs;
// Frame to display histogram of an image
class HistogramFrame : public wxFrame {
public:
    HistogramFrame(wxWindow* parent, const wxImage& img)
        : wxFrame(parent, wxID_ANY, "Histogram", wxDefaultPosition, wxSize(420, 200)) {
        if (!img.IsOk()) {
            new wxStaticText(this, wxID_ANY, "No image", wxDefaultPosition);
            return;
        }

        // Compute luminance histogram (grayscale)
        vector<int> hist(256, 0);
        unsigned char* data = img.GetData();
        int w = img.GetWidth(), h = img.GetHeight();

        for (int i = 0; i < w * h; ++i) {
            int r = data[i * 3 + 0];
            int g = data[i * 3 + 1];
            int b = data[i * 3 + 2];
            int lum = (int)round(0.299 * r + 0.587 * g + 0.114 * b); // Luminosity formula
            lum = clamp(lum, 0, 255);
            ++hist[lum];
        }

        int maxVal = *max_element(hist.begin(), hist.end()); // For normalization
        wxBitmap bmp(256, 100);
        wxMemoryDC dc(bmp);
        dc.SetBackground(*wxWHITE_BRUSH);
        dc.Clear();

        // Draw histogram bars
        for (int x = 0; x < 256; ++x) {
            int barHeight = (maxVal > 0) ? (int)((double)hist[x] * 100.0 / maxVal) : 0;
            dc.DrawRectangle(x, 100 - barHeight, 1, barHeight);
        }
        dc.SelectObject(wxNullBitmap);

        wxStaticBitmap* sb = new wxStaticBitmap(this, wxID_ANY, bmp);
        wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(sb, 1, wxEXPAND | wxALL, 10);
        SetSizer(sizer);
    }
};

// Frame to view a stack of images (slices)
class StackViewer : public wxFrame {
public:
    StackViewer(wxWindow* parent, const vector<wxImage>& slices)
        : wxFrame(parent, wxID_ANY, "Stack Viewer", wxDefaultPosition, wxSize(800, 600)),
        m_slices(slices), m_index(0) {
        wxBoxSizer* vbox = new wxBoxSizer(wxVERTICAL);

        if (m_slices.empty()) {
            vbox->Add(new wxStaticText(this, wxID_ANY, "No slices to display"), 1, wxALL | wxALIGN_CENTER, 10);
            SetSizer(vbox);
            return;
        }

        // Display first slice
        m_bitmap = new wxStaticBitmap(this, wxID_ANY, wxBitmap(m_slices[0]));
        m_slider = new wxSlider(this, wxID_ANY, 0, 0, (int)m_slices.size() - 1);

        wxBoxSizer* bottom = new wxBoxSizer(wxHORIZONTAL);
        m_label = new wxStaticText(this, wxID_ANY, GetSliceLabel());

        vbox->Add(m_bitmap, 1, wxEXPAND);
        vbox->Add(m_slider, 0, wxEXPAND | wxALL, 5);
        bottom->Add(m_label, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 10);
        vbox->Add(bottom, 0, wxEXPAND);
        SetSizer(vbox);

        // Bind slider to slice change
        m_slider->Bind(wxEVT_SLIDER, &StackViewer::OnSlide, this);
    }

private:
    vector<wxImage> m_slices;        // Image stack
    int m_index = 0;                  // Current slice index
    wxStaticBitmap* m_bitmap{ nullptr };
    wxSlider* m_slider{ nullptr };
    wxStaticText* m_label{ nullptr };

    // Update displayed slice when slider moves
    void OnSlide(wxCommandEvent& /*evt*/) {
        if (m_slices.empty() || !m_bitmap) return;
        m_index = m_slider->GetValue();
        if (m_index >= 0 && m_index < (int)m_slices.size()) {
            m_bitmap->SetBitmap(wxBitmap(m_slices[m_index]));
            if (m_label) m_label->SetLabel(GetSliceLabel());
        }
    }

    wxString GetSliceLabel() const { return wxString::Format("Slice %d / %d", m_index + 1, (int)m_slices.size()); }
};

// Scrollable panel to display and interact with a single image
class ImagePanel : public wxScrolledWindow
{
public:
    ImagePanel(wxWindow* parent)
        : wxScrolledWindow(parent, wxID_ANY) {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetBackgroundColour(*wxWHITE);
        SetScrollRate(10, 10);

        // Bind events
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

    // Copy selection to internal clipboard
    void OnCopy(wxCommandEvent&) { CopySelection(); }
    void OnPaste(wxCommandEvent&) { PasteClipboard(wxPoint(10, 10), BLEND); }
    void OnUndo(wxCommandEvent&) { Undo(); }

    // Save current image to disk
    void OnSave(wxCommandEvent&) {
        wxFileDialog dlg(this, "Save Image", "", "", "PNG files (*.png)|*.png", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
        if (dlg.ShowModal() == wxID_OK) {
            if (!m_originalImg.SaveFile(dlg.GetPath(), wxBITMAP_TYPE_PNG))
                wxMessageBox("Failed to save image", "Save", wxICON_ERROR);
        }
    }

    // Set new image and manage undo history
    void SetImage(const wxImage& img) {
        if (m_originalImg.IsOk()) {
            m_history.push_back(m_originalImg);
            if (m_history.size() > MAX_HISTORY) m_history.erase(m_history.begin());
        }
        m_originalImg = img;
        ZoomFit();
    }

    ROIManager m_roiManager;              // ROI manager
    wxImage GetOriginalImage() const { return m_originalImg; }
    wxRect GetSelectionRect() const { return m_selection; }
    double GetZoomFactor() const { return m_zoomFactor; }

    void ClearSelection() { m_selection = wxRect(); Refresh(); }

    // Zoom controls
    void ZoomIn() { m_zoomFactor *= 1.2; m_fitMode = false; ApplyZoom(); }
    void ZoomOut() { m_zoomFactor /= 1.2; if (m_zoomFactor < 0.01) m_zoomFactor = 0.01; m_fitMode = false; ApplyZoom(); }
    void ZoomFit() { m_fitMode = true; ApplyZoom(); }

    // Copy selected region
    void CopySelection() {
        if (!m_selection.IsEmpty() && m_originalImg.IsOk())
            m_clipboard = m_originalImg.GetSubImage(m_selection);
    }

    // Keyboard shortcuts for Ctrl+C, V, Z
    void OnKeyDown(wxKeyEvent& event) {
        if (event.ControlDown()) {
            int key = event.GetKeyCode();
            if (key == 'C') CopySelection();
            else if (key == 'V') PasteClipboard(wxPoint(10, 10), BLEND);
            else if (key == 'Z') Undo();
            else event.Skip();
        }
        else { event.Skip(); }
    }

    // Undo last image change
    void Undo() {
        if (!m_history.empty()) {
            m_originalImg = m_history.back();
            m_history.pop_back();
            ZoomFit();
        }
        else {
            wxMessageBox("No previous image to undo.", "Undo", wxICON_INFORMATION);
        }
    }

private:
    wxBitmap m_bitmap;                   // Display bitmap
    wxImage m_originalImg;               // Original image data
    double m_zoomFactor = 1.0;          // Zoom factor
    bool m_fitMode = true;               // Fit-to-window flag
    wxRect m_selection;                  // User selection rectangle
    bool m_selecting = false;            // True when dragging selection
    wxPoint m_startPoint;                // Start of selection
    wxImage m_clipboard;                 // Copy/paste buffer
    bool m_showROIs = false;             // Display ROIs

    enum BlendMode { AND, OR, XOR, BLEND }; // Blend modes for pasting
    enum DrawMode { NONE, TEXT, RECT, ELLIPSE, ARROW, POLYGON }; // Drawing modes
    DrawMode m_drawMode = NONE;
    vector<wxImage> m_history;           // Undo history

    // Apply zoom or fit-to-window
    void ApplyZoom() {
        if (!m_originalImg.IsOk()) return;

        wxSize panelSize = GetClientSize();
        int newW = (int)(m_originalImg.GetWidth() * m_zoomFactor);
        int newH = (int)(m_originalImg.GetHeight() * m_zoomFactor);

        if (m_fitMode && m_originalImg.GetWidth() > 0 && m_originalImg.GetHeight() > 0) {
            double scaleX = (double)panelSize.x / m_originalImg.GetWidth();
            double scaleY = (double)panelSize.y / m_originalImg.GetHeight();
            m_zoomFactor = min(scaleX, scaleY);
            if (m_zoomFactor <= 0) m_zoomFactor = 1.0;
            newW = (int)(m_originalImg.GetWidth() * m_zoomFactor);
            newH = (int)(m_originalImg.GetHeight() * m_zoomFactor);
        }

        wxImage scaled = m_originalImg.Scale(newW, newH, wxIMAGE_QUALITY_HIGH);
        m_bitmap = wxBitmap(scaled);

        SetVirtualSize(newW, newH);
        Refresh();

        wxFrame* frame = dynamic_cast<wxFrame*>(GetParent());
        if (frame) {
            wxString zoomStr = m_fitMode ? wxString("Zoom: Fit") : wxString::Format("Zoom: %.0f%%", m_zoomFactor * 100);
            frame->SetStatusText(zoomStr, 1);
        }
    }

    // Context menu on right-click
    void OnContextMenu(wxContextMenuEvent& event) {
        wxMenu menu;
        menu.Append(wxID_COPY, "Copy Selection");
        menu.Append(wxID_PASTE, "Paste Clipboard");
        menu.Append(wxID_SAVE, "Save Image As...");
        menu.AppendSeparator();
        menu.Append(wxID_UNDO, "Undo");

        wxPoint pos = event.GetPosition();
        if (pos == wxDefaultPosition) pos = ScreenToClient(wxGetMousePosition());
        PopupMenu(&menu, pos);
    }

    // Paint event
    void OnPaint(wxPaintEvent& /*event*/) {
        wxAutoBufferedPaintDC dc(this);
        DoPrepareDC(dc);
        dc.Clear();

        if (m_bitmap.IsOk()) dc.DrawBitmap(m_bitmap, 0, 0, true);
        else {
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.DrawText("No image loaded", 10, 10);
        }

        // Draw ROIs
        if (m_showROIs) {
            for (const auto& roi : m_roiManager.GetROIs()) {
                dc.SetPen(*wxGREEN_PEN);
                dc.SetBrush(*wxTRANSPARENT_BRUSH);
                dc.DrawRectangle(roi);
            }
        }

        // Draw selection rectangle
        if (!m_selection.IsEmpty()) {
            dc.SetPen(*wxRED_PEN);
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            dc.DrawRectangle(m_selection);
        }

        if (m_drawMode == RECT && !m_selection.IsEmpty()) {
            dc.SetPen(*wxBLUE_PEN);
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            dc.DrawRectangle(m_selection);
        }
    }

    void ToggleROIs() { m_showROIs = !m_showROIs; Refresh(); }

    // Mouse events
    void OnLeftDown(wxMouseEvent& event) {
        m_startPoint = CalcUnscrolledPosition(event.GetPosition());
        m_selecting = true;
        CaptureMouse();
    }

    void OnLeftUp(wxMouseEvent& event) {
        if (m_selecting) {
            wxPoint endPoint = CalcUnscrolledPosition(event.GetPosition());
            int x1 = min(m_startPoint.x, endPoint.x);
            int y1 = min(m_startPoint.y, endPoint.y);
            int x2 = max(m_startPoint.x, endPoint.x);
            int y2 = max(m_startPoint.y, endPoint.y);
            // Clamp and set selection rectangle
            x1 = max(0, x1); y1 = max(0, y1);
            x2 = max(x1, x2); y2 = max(y1, y2);
            m_selection = wxRect(wxPoint(x1, y1), wxSize(x2 - x1 + 1, y2 - y1 + 1));
            m_selecting = false;
            if (HasCapture()) ReleaseMouse();
            Refresh();
        }
    }

    void OnMouseMove(wxMouseEvent& event) {
        wxPoint pos = CalcUnscrolledPosition(event.GetPosition());

        if (event.Dragging() && event.LeftIsDown() && m_selecting) {
            int x1 = min(m_startPoint.x, pos.x);
            int y1 = min(m_startPoint.y, pos.y);
            int x2 = max(m_startPoint.x, pos.x);
            int y2 = max(m_startPoint.y, pos.y);
            x1 = max(0, x1); y1 = max(0, y1);
            x2 = max(x1, x2); y2 = max(y1, y2);
            m_selection = wxRect(wxPoint(x1, y1), wxSize(x2 - x1 + 1, y2 - y1 + 1));
            Refresh();
        }

        ShowPixelInfo(pos);
    }

    void OnMouseWheel(wxMouseEvent& event) {
        int rot = event.GetWheelRotation();
        if (rot > 0) ZoomIn(); else ZoomOut();
    }

    void ShowPixelInfo(const wxPoint& pos) {
        if (!m_originalImg.IsOk()) return;
        if (pos.x < 0 || pos.y < 0 || pos.x >= m_originalImg.GetWidth() || pos.y >= m_originalImg.GetHeight()) return;

        unsigned char* data = m_originalImg.GetData();
        int idx = (pos.y * m_originalImg.GetWidth() + pos.x) * 3;
        int r = data[idx];
        int g = data[idx + 1];
        int b = data[idx + 2];

        wxFrame* frame = dynamic_cast<wxFrame*>(GetParent());
        if (frame) {
            frame->SetStatusText(wxString::Format("X: %d Y: %d R: %d G: %d B: %d", pos.x, pos.y, r, g, b), 0);
        }
    }

    void CutSelection() {
        CopySelection();
        if (!m_originalImg.IsOk() || m_selection.IsEmpty()) return;

        wxImage img = m_originalImg;
        unsigned char* data = img.GetData();
        int w = img.GetWidth(), h = img.GetHeight();

        int x0 = clamp(m_selection.x, 0, w - 1);
        int y0 = clamp(m_selection.y, 0, h - 1);
        int x1 = clamp(m_selection.x + m_selection.GetWidth() - 1, 0, w - 1);
        int y1 = clamp(m_selection.y + m_selection.GetHeight() - 1, 0, h - 1);

        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                int idx = (y * w + x) * 3;
                data[idx] = data[idx + 1] = data[idx + 2] = 255; // white-out
            }
        }
        SetImage(img);
    }

    void PasteClipboard(wxPoint dest, BlendMode mode) {
        if (!m_clipboard.IsOk() || !m_originalImg.IsOk()) return;

        wxImage img = m_originalImg;
        unsigned char* dst = img.GetData();
        unsigned char* src = m_clipboard.GetData();
        int w = m_clipboard.GetWidth(), h = m_clipboard.GetHeight();

        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                int dx = dest.x + x, dy = dest.y + y;
                if (dx >= img.GetWidth() || dy >= img.GetHeight() || dx < 0 || dy < 0) continue;

                int di = (dy * img.GetWidth() + dx) * 3;
                int si = (y * w + x) * 3;

                for (int c = 0; c < 3; ++c) {
                    switch (mode) {
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

static inline bool InBounds(int x, int y, int w, int h) {
    return (x >= 0 && y >= 0 && x < w && y < h);
}

static inline unsigned char GetGray(const wxImage& img, int x, int y) {
    const int w = img.GetWidth();
    const unsigned char* data = img.GetData();
    return data[(y * w + x) * 3]; // grayscale stored in all channels
}

static double CircularAverageNearest(const wxImage& img, int cx, int cy, int R, int* outUniqueSamples = nullptr) {
    if (!img.IsOk() || R <= 0) return numeric_limits<double>::quiet_NaN();

    const int w = img.GetWidth();
    const int h = img.GetHeight();

    // If the circle is completely outside, early out (optional)
    if (cx + R < 0 || cx - R >= w || cy + R < 0 || cy - R >= h)
        return numeric_limits<double>::quiet_NaN();

    const int N = max(8, (int)lround(2.0 * PI * (double)R));

    // Pack (x,y) into 64-bit key to dedupe
    unordered_set<long long> visited;
    visited.reserve((size_t)N * 2);

    double sum = 0.0;
    int count = 0;

    for (int k = 0; k < N; ++k) {
        const double theta = (2.0 * PI * (double)k) / (double)N;
        const double fx = (double)cx + (double)R * cos(theta);
        const double fy = (double)cy + (double)R * sin(theta);

        const int x = (int)lround(fx);
        const int y = (int)lround(fy);

        if (!InBounds(x, y, w, h)) continue;

        const long long key = ((long long)x << 32) ^ (unsigned int)y;
        if (visited.insert(key).second) {
            sum += (double)GetGray(img, x, y);
            ++count;
        }
    }

    if (outUniqueSamples) *outUniqueSamples = count;
    if (count == 0) return numeric_limits<double>::quiet_NaN();
    return sum / (double)count;
}

class PlotFrame : public wxFrame {
public:
    PlotFrame(wxWindow* parent, const std::vector<RadialAvgPoint>& data)
        : wxFrame(parent, wxID_ANY, "Radial Average Plot", wxDefaultPosition, wxSize(700, 450)),
        m_data(data)
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        Bind(wxEVT_PAINT, &PlotFrame::OnPaint, this);
        Bind(wxEVT_SIZE, &PlotFrame::OnResize, this);
    }

private:
    std::vector<RadialAvgPoint> m_data;

    void OnResize(wxSizeEvent& evt) {
        Refresh();
        evt.Skip();
    }

    void OnPaint(wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(this);
        dc.Clear();

        if (m_data.empty()) {
            dc.DrawText("No data. Run a radial sweep first.", 10, 10);
            return;
        }

        // --- 1) Compute plot bounds (data min/max) ---
        int minR = numeric_limits<int>::max();
        int maxR = numeric_limits<int>::min();
        double minY = numeric_limits<double>::infinity();
        double maxY = -numeric_limits<double>::infinity();

        for (const auto& p : m_data) {
            if (!isfinite(p.avg)) continue;
            minR = min(minR, p.R);
            maxR = max(maxR, p.R);
            minY = min(minY, p.avg);
            maxY = max(maxY, p.avg);
        }

        if (minR == numeric_limits<int>::max()) {
            dc.DrawText("All data points are invalid (NaN).", 10, 10);
            return;
        }

        // Avoid zero range
        if (maxR == minR) maxR = minR + 1;
        if (maxY == minY) { maxY = minY + 1.0; }

        // --- 2) Layout (margins + axes box) ---
        const wxSize sz = GetClientSize();
        const int left = 60, right = 20, top = 20, bottom = 50;
        const int plotW = max(1, sz.x - left - right);
        const int plotH = max(1, sz.y - top - bottom);

        wxRect plotRect(left, top, plotW, plotH);

        // Axes
        dc.DrawLine(plotRect.GetLeft(), plotRect.GetBottom(),
            plotRect.GetRight(), plotRect.GetBottom()); // X axis
        dc.DrawLine(plotRect.GetLeft(), plotRect.GetTop(),
            plotRect.GetLeft(), plotRect.GetBottom()); // Y axis

        // Labels
        dc.DrawText("R (pixels)", plotRect.GetLeft() + plotW / 2 - 30, sz.y - 30);
        dc.DrawText("Avg", 10, plotRect.GetTop() + plotH / 2 - 10);

        // Simple min/max annotations
        dc.DrawText(wxString::Format("%d", minR), plotRect.GetLeft(), plotRect.GetBottom() + 5);
        dc.DrawText(wxString::Format("%d", maxR), plotRect.GetRight() - 30, plotRect.GetBottom() + 5);
        dc.DrawText(wxString::Format("%.1f", maxY), plotRect.GetLeft() - 55, plotRect.GetTop());
        dc.DrawText(wxString::Format("%.1f", minY), plotRect.GetLeft() - 55, plotRect.GetBottom() - 15);

        // --- 3) Mapping functions data -> screen ---
        auto mapX = [&](int R) -> int {
            double t = (double)(R - minR) / (double)(maxR - minR);
            return plotRect.GetLeft() + (int)std::lround(t * plotW);
            };
        auto mapY = [&](double y) -> int {
            double t = (y - minY) / (maxY - minY);
            // invert Y (screen grows downward)
            return plotRect.GetBottom() - (int)std::lround(t * plotH);
            };

        // --- 4) Draw polyline ---
        vector<wxPoint> pts;
        pts.reserve(m_data.size());

        for (const auto& p : m_data) {
            if (!isfinite(p.avg)) continue;
            pts.emplace_back(mapX(p.R), mapY(p.avg));
        }

        if (pts.size() >= 2) {
            dc.DrawLines((int)pts.size(), pts.data());
        }
        else if (pts.size() == 1) {
            dc.DrawCircle(pts[0], 2);
        }
    }
};

class ImageFrame : public wxFrame {
public:
    ImageFrame(wxWindow* parent, const wxString& filepath)
        : wxFrame(parent, wxID_ANY, "Image Display", wxDefaultPosition, wxSize(820, 750)) {
        wxBoxSizer* vbox = new wxBoxSizer(wxVERTICAL);

        m_imagePanel = new ImagePanel(this);

        wxToolBar* toolbar = new wxToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTB_HORIZONTAL | wxNO_BORDER);
        toolbar->AddTool(wxID_ZOOM_IN, "Zoom In", CreateLabeledBitmap("+"));
        toolbar->AddTool(wxID_ZOOM_OUT, "Zoom Out", CreateLabeledBitmap("-"));
        toolbar->AddTool(wxID_ZOOM_100, "Fit", CreateLabeledBitmap("Fit"));
        toolbar->AddSeparator();

        m_rotateId = wxWindow::NewControlId();
        m_helpId = wxWindow::NewControlId();
        m_flipHId = wxWindow::NewControlId();
        m_flipVId = wxWindow::NewControlId();
        m_cropId = wxWindow::NewControlId();
        m_resizeId = wxWindow::NewControlId();
        m_copyId = wxWindow::NewControlId();
        m_undoId = wxWindow::NewControlId();
        m_pluginId = wxWindow::NewControlId();
        m_circAvgId = wxWindow::NewControlId();
        m_radialSweepId = wxWindow::NewControlId();
        m_exportCsvId = wxWindow::NewControlId(); // optional
        m_plotId = wxWindow::NewControlId();

        toolbar->AddTool(m_rotateId, "Rotate 90\xC2\xB0", CreateLabeledBitmap("R90"));
        toolbar->AddTool(m_flipHId, "Flip H", CreateLabeledBitmap("FH"));
        toolbar->AddTool(m_flipVId, "Flip V", CreateLabeledBitmap("FV"));
        toolbar->AddTool(m_cropId, "Crop", CreateLabeledBitmap("Crop"));
        toolbar->AddTool(m_resizeId, "Resize", CreateLabeledBitmap("Size"));
        toolbar->AddTool(m_copyId, "Copy", CreateLabeledBitmap("Copy"));
        toolbar->AddTool(m_undoId, "Undo", CreateLabeledBitmap("Undo"));
        toolbar->AddSeparator();
        toolbar->AddTool(m_pluginId, "Load Plugin", CreateLabeledBitmap("Plug"));
        toolbar->AddTool(m_helpId, "Help", CreateLabeledBitmap("?"));
        toolbar->AddTool(m_circAvgId, "CircAvg", CreateLabeledBitmap("CA"));
        toolbar->AddTool(m_radialSweepId, "RadialSweep", CreateLabeledBitmap("RS"));
        toolbar->AddTool(m_exportCsvId, "ExportCSV", CreateLabeledBitmap("CSV"));
        toolbar->AddTool(m_plotId, "Plot", CreateLabeledBitmap("Plot"));
        toolbar->Realize();

        vbox->Add(toolbar, 0, wxEXPAND);
        vbox->Add(m_imagePanel, 1, wxEXPAND);

        m_resultsFrame = new ResultsFrame(this);
        m_resultsFrame->Show();

        SetSizer(vbox);
        CreateStatusBar(2);
        SetStatusText("Ready", 0);

        LoadImage(filepath);

        if (m_imagePanel->GetOriginalImage().IsOk()) {
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
        Bind(wxEVT_TOOL, &ImageFrame::OnLoadPlugin, this, m_pluginId);
        Bind(wxEVT_TOOL, &ImageFrame::OnCircularAverage, this, m_circAvgId);
        Bind(wxEVT_TOOL, &ImageFrame::OnRadialSweep, this, m_radialSweepId);
        Bind(wxEVT_TOOL, &ImageFrame::OnExportRadialCSV, this, m_exportCsvId);
        Bind(wxEVT_TOOL, &ImageFrame::OnShowPlot, this, m_plotId);

        Centre();
    }

private:
    ImagePanel* m_imagePanel{ nullptr };
    ResultsFrame* m_resultsFrame{ nullptr };

    vector<RadialAvgPoint> m_radialAvgData;

    int m_rotateId;
    int m_flipHId;
    int m_flipVId;
    int m_cropId;
    int m_resizeId;
    int m_copyId;
    int m_undoId;
    int m_helpId;
    int m_pluginId;
    int m_circAvgId;
    int m_radialSweepId;
    int m_exportCsvId;
    int m_plotId;

    wxBitmap CreateLabeledBitmap(const wxString& label) {
        wxBitmap bmp(24, 24);
        wxMemoryDC dc(bmp);
        dc.SetBackground(*wxWHITE_BRUSH);
        dc.Clear();
        dc.DrawText(label, 4, 4);
        dc.SelectObject(wxNullBitmap);
        return bmp;
    }

    void OnLoadPlugin(wxCommandEvent&) {
#ifdef __WXMSW__
        const wxString pluginFilter = "DLLs (*.dll)|*.dll";
#else
        const wxString pluginFilter = "Shared objects (*.so)|*.so";
#endif
        wxFileDialog pdlg(this, "Select Plugin", "", "", pluginFilter, wxFD_OPEN);
        if (pdlg.ShowModal() == wxID_OK) {
            wxImage img = m_imagePanel->GetOriginalImage();
            if (img.IsOk() && PluginLoader::LoadPlugin(pdlg.GetPath(), img)) {
                m_imagePanel->SetImage(img);
                m_resultsFrame->AddResult("Applied plugin successfully.");
            }
            else {
                wxMessageBox("Failed to load/apply plugin", "Plugin", wxICON_WARNING);
            }
        }
    }

    void LoadImage(const wxString& filepath) {
        if (filepath.IsEmpty()) return;
        ifstream file(filepath.mb_str(), ios::binary);
        if (!file) {
            wxMessageBox("Failed to open file: " + filepath, "Open", wxICON_ERROR);
            return;
        }

        // read to end to determine size
        file.seekg(0, ios::end);
        streamoff sz = file.tellg();
        if (sz <= 0 || sz < (streamoff)HEADER_OFFSET) {
            wxMessageBox("File too small or invalid format", "Open", wxICON_ERROR);
            return;
        }

        file.seekg(HEADER_OFFSET, ios::beg);
        // For legacy format we use fixed dimensions; if file too small, bail out
        const int WIDTH = 2082, HEIGHT = 2217, PIXEL_DEPTH = 4;
        const streamoff expected = (streamoff)WIDTH * HEIGHT * PIXEL_DEPTH;
        if (sz - HEADER_OFFSET < expected) {
            wxMessageBox("File does not contain expected image data (size mismatch).", "Open", wxICON_ERROR);
            return;
        }

        vector<unsigned char> buffer((size_t)expected);
        file.read(reinterpret_cast<char*>(buffer.data()), expected);
        if (file.gcount() < expected) {
            wxMessageBox("Failed to read image data.", "Open", wxICON_ERROR);
            return;
        }

        wxImage img(WIDTH, HEIGHT, true);
        unsigned char* rgb = img.GetData();
        if (!rgb) { wxMessageBox("Failed to allocate image buffer.", "Open", wxICON_ERROR); return; }

        for (int i = 0; i < WIDTH * HEIGHT; ++i) {
            unsigned char r = buffer[i * 4 + 2];
            unsigned char g = buffer[i * 4 + 1];
            unsigned char b = buffer[i * 4 + 0];
            unsigned char grey = (unsigned char)round(0.299 * r + 0.587 * g + 0.114 * b);
            rgb[i * 3 + 0] = grey;
            rgb[i * 3 + 1] = grey;
            rgb[i * 3 + 2] = grey;
        }
        m_imagePanel->SetImage(img);

        if (m_resultsFrame) {
            m_resultsFrame->AddResult(wxString::Format("Loaded image: %s", filepath));
            m_resultsFrame->AddResult(wxString::Format("Width: %d, Height: %d", img.GetWidth(), img.GetHeight()));
            m_resultsFrame->AddResult("Successfully loaded and converted to grayscale.");
        }
    }

    void OnZoomIn(wxCommandEvent&) { m_imagePanel->ZoomIn(); }
    void OnZoomOut(wxCommandEvent&) { m_imagePanel->ZoomOut(); }
    void OnZoomFit(wxCommandEvent&) { m_imagePanel->ZoomFit(); }
    void OnCopy(wxCommandEvent&) { m_imagePanel->CopySelection(); }
    void OnResizeEvent(wxSizeEvent& evt) { m_imagePanel->ZoomFit(); evt.Skip(); }

    void OnRotate90(wxCommandEvent&) {
        wxImage img = m_imagePanel->GetOriginalImage();
        if (img.IsOk()) { img = img.Rotate90(true); m_imagePanel->SetImage(img); }
    }
    void OnFlipH(wxCommandEvent&) {
        wxImage img = m_imagePanel->GetOriginalImage();
        if (img.IsOk()) { img = img.Mirror(false); m_imagePanel->SetImage(img); }
    }
    void OnFlipV(wxCommandEvent&) {
        wxImage img = m_imagePanel->GetOriginalImage();
        if (img.IsOk()) { img = img.Mirror(true); m_imagePanel->SetImage(img); }
    }

    void OnCrop(wxCommandEvent&)
    {
        wxRect rect = m_imagePanel->GetSelectionRect();
        wxImage img = m_imagePanel->GetOriginalImage();

        if (!rect.IsEmpty() && img.IsOk()) {
            double invZoom = 1.0 / m_imagePanel->GetZoomFactor();
            wxRect imgRect(
                (int)(rect.x * invZoom),
                (int)(rect.y * invZoom),
                (int)(rect.width * invZoom),
                (int)(rect.height * invZoom)
            );

            if (imgRect.x >= 0 && imgRect.y >= 0 &&
                imgRect.GetRight() <= img.GetWidth() - 1 &&
                imgRect.GetBottom() <= img.GetHeight() - 1)
            {
                wxImage cropped = img.GetSubImage(imgRect);
                m_imagePanel->SetImage(cropped);
                m_imagePanel->ZoomFit();
                m_imagePanel->ClearSelection();
            }
            else {
                wxMessageBox("Invalid selection for cropping.", "Crop", wxICON_INFORMATION);
            }
        }
    }

    void OnUndo(wxCommandEvent&) { m_imagePanel->Undo(); }

    void OnResize(wxCommandEvent&) {
        wxImage img = m_imagePanel->GetOriginalImage();
        if (!img.IsOk()) return;

        wxTextEntryDialog dlg(this, "Enter new size: width,height", "Resize Image", wxString::Format("%d,%d", img.GetWidth(), img.GetHeight()));
        if (dlg.ShowModal() == wxID_OK) {
            wxString val = dlg.GetValue();
            wxString wStr = val.BeforeFirst(',');
            wxString hStr = val.AfterFirst(',');

            long w, h;
            if (wStr.ToLong(&w) && hStr.ToLong(&h) && w > 0 && h > 0) {
                wxImage resized = img.Scale((int)w, (int)h, wxIMAGE_QUALITY_HIGH);
                m_imagePanel->SetImage(resized);
            }
            else {
                wxMessageBox("Invalid input format or dimensions. Use positive width,height", "Resize", wxICON_ERROR);
            }
        }
    }

    void OnHelp(wxCommandEvent&) {
        wxString helpText =
            "Toolbar Button Guide:\n\n"
            "+ : Zoom In\n"
            "- : Zoom Out\n"
            "Fit : Fit image to window\n"
            "R90 : Rotate image 90 degrees\n"
            "FH : Flip image horizontally\n"
            "FV : Flip image vertically\n"
            "Crop : Crop selected region\n"
            "Size : Resize image\n"
            "Copy : Copy selected region\n"
            "Plug : Load and apply an image filter plugin (.dll/.so)\n"
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

    void OnCircularAverage(wxCommandEvent&) {
        wxImage img = m_imagePanel->GetOriginalImage();
        if (!img.IsOk()) return;

        wxTextEntryDialog dlg(this, "Enter radius R in pixels (e.g., 300)", "Circular Average", "300");
        if (dlg.ShowModal() != wxID_OK) return;

        long R;
        if (!dlg.GetValue().ToLong(&R) || R <= 0) {
            wxMessageBox("Please enter a positive integer radius.", "Circular Average", wxICON_WARNING);
            return;
        }

        // Phase 1: center = image center
        const int cx = img.GetWidth() / 2;
        const int cy = img.GetHeight() / 2;

        int uniqueSamples = 0;
        const double avg = CircularAverageNearest(img, cx, cy, (int)R, &uniqueSamples);

        if (!std::isfinite(avg)) {
            m_resultsFrame->AddResult(wxString::Format("R=%ld: no valid samples (circle outside image?).", R));
            return;
        }

        m_resultsFrame->AddResult(wxString::Format(
            "Circular average (nearest) | center=(%d,%d) R=%ld | uniqueSamples=%d | avg=%.3f",
            cx, cy, R, uniqueSamples, avg
        ));
    }

    void OnRadialSweep(wxCommandEvent&) {
        wxImage img = m_imagePanel->GetOriginalImage();
        if (!img.IsOk()) return;

        // Input: "Rmin,Rmax,step"
        wxTextEntryDialog dlg(this,
            "Enter Rmin,Rmax,step (e.g., 0,600,5)",
            "Radial Average Sweep",
            "0,600,5"
        );
        if (dlg.ShowModal() != wxID_OK) return;

        long Rmin = 0, Rmax = 0, step = 1;
        {
            wxString s = dlg.GetValue();
            wxArrayString parts = wxSplit(s, ',');
            if (parts.size() != 3 ||
                !parts[0].ToLong(&Rmin) ||
                !parts[1].ToLong(&Rmax) ||
                !parts[2].ToLong(&step) ||
                step <= 0 || Rmax < Rmin) {
                wxMessageBox("Invalid input. Use Rmin,Rmax,step like 0,600,5", "Radial Sweep", wxICON_WARNING);
                return;
            }
        }

        // Center choice: Phase 2 uses image center
        const int cx = img.GetWidth() / 2;
        const int cy = img.GetHeight() / 2;

        // Clear old results
        m_radialAvgData.clear();
        m_radialAvgData.reserve((size_t)((Rmax - Rmin) / step + 1));

        int validCount = 0;
        for (int R = (int)Rmin; R <= (int)Rmax; R += (int)step) {
            int samples = 0;
            double avg = CircularAverageNearest(img, cx, cy, R, &samples);

            // Store NaN too if you want, but usually keep only valid
            if (std::isfinite(avg) && samples > 0) {
                m_radialAvgData.push_back({ R, avg, samples });
                ++validCount;
            }
            else {
                // keep invalid points if you want gaps later; your call:
                m_radialAvgData.push_back({ R, std::numeric_limits<double>::quiet_NaN(), samples });
            }
        }

        m_resultsFrame->AddResult(wxString::Format(
            "Radial sweep complete. center=(%d,%d)  R=[%ld..%ld] step=%ld  points=%zu  valid=%d",
            cx, cy, Rmin, Rmax, step, m_radialAvgData.size(), validCount
        ));

        // Print a small preview (first 5 + last 5)
        auto printPoint = [&](const RadialAvgPoint& p) {
            if (std::isfinite(p.avg))
                m_resultsFrame->AddResult(wxString::Format("R=%d  avg=%.3f  samples=%d", p.R, p.avg, p.samples));
            else
                m_resultsFrame->AddResult(wxString::Format("R=%d  avg=NaN  samples=%d", p.R, p.samples));
            };

        const size_t n = m_radialAvgData.size();
        for (size_t i = 0; i < n && i < 5; ++i) printPoint(m_radialAvgData[i]);
        if (n > 10) m_resultsFrame->AddResult("...");
        for (size_t i = (n > 5 ? std::max((size_t)5, n - 5) : 0); i < n; ++i) {
            if (i >= 5 && i < n - 5) continue;
            printPoint(m_radialAvgData[i]);
        }
    }

    void OnExportRadialCSV(wxCommandEvent&) {
        if (m_radialAvgData.empty()) {
            wxMessageBox("No radial data to export. Run a sweep first.", "Export CSV", wxICON_INFORMATION);
            return;
        }

        wxFileDialog saveDlg(this, "Save radial averages as CSV", "", "radial_avg.csv",
            "CSV files (*.csv)|*.csv", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
        if (saveDlg.ShowModal() != wxID_OK) return;

        std::ofstream out(saveDlg.GetPath().ToStdString());
        if (!out) {
            wxMessageBox("Could not open file for writing.", "Export CSV", wxICON_ERROR);
            return;
        }

        out << "R,avg,samples\n";
        for (const auto& p : m_radialAvgData) {
            out << p.R << ",";
            if (isfinite(p.avg)) out << p.avg;
            out << "," << p.samples << "\n";
        }

        m_resultsFrame->AddResult("Exported radial averages to CSV: " + saveDlg.GetPath());
    }

    void OnShowPlot(wxCommandEvent&) {
        if (m_radialAvgData.empty()) {
            wxMessageBox("No radial data to plot. Run RadialSweep first.", "Plot", wxICON_INFORMATION);
            return;
        }
        auto* pf = new PlotFrame(this, m_radialAvgData);
        pf->Show();
    }
};

class FileBrowser : public wxPanel {
public:
    FileBrowser(wxWindow* parent)
        : wxPanel(parent, wxID_ANY) {
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
    wxListCtrl* m_listCtrl{ nullptr };
    vector<wxFileName> m_items;

    void UpdateList() {
        m_listCtrl->DeleteAllItems();

        for (const auto& fn : m_items) {
            long idx = m_listCtrl->InsertItem(m_listCtrl->GetItemCount(), fn.GetFullName());
            wxString fullPath = fn.GetFullPath();

            if (wxDirExists(fullPath)) {
                m_listCtrl->SetItem(idx, 1, "Folder");
                m_listCtrl->SetItem(idx, 2, FormatSize(GetFolderSize(fn)));
            }
            else if (wxFileExists(fullPath)) {
                wxString ext = fn.GetExt();
                if (ext.IsEmpty()) ext = "File";
                m_listCtrl->SetItem(idx, 1, ext);
                m_listCtrl->SetItem(idx, 2, FormatSize(fn.GetSize()));
            }
            else {
                // fallback (e.g., missing or invalid path)
                m_listCtrl->SetItem(idx, 1, "Unknown");
                m_listCtrl->SetItem(idx, 2, "0.00 B");
            }
        }
    }

    void OnAddFile(wxCommandEvent&) {
        wxFileDialog dlg(this, "Select files", "", "", "*.*", wxFD_OPEN | wxFD_FILE_MUST_EXIST | wxFD_MULTIPLE);
        if (dlg.ShowModal() != wxID_OK) return;

        wxArrayString paths;
        dlg.GetPaths(paths);
        for (const auto& p : paths) m_items.push_back(wxFileName(p));
        UpdateList();
    }

    void OnAddFolder(wxCommandEvent&) {
        wxDirDialog dlg(this, "Select folder");
        if (dlg.ShowModal() != wxID_OK) return;
        m_items.push_back(wxFileName(dlg.GetPath()));
        UpdateList();
    }

    void OnDeleteSelected(wxCommandEvent&) {
        long sel = m_listCtrl->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
        if (sel != -1) {
            m_items.erase(m_items.begin() + sel);
            UpdateList();
        }
    }

    void OnItemActivated(wxListEvent& event) {
        wxFileName fn = m_items[event.GetIndex()];
        if (fn.FileExists()) {
            auto* frame = new ImageFrame(nullptr, fn.GetFullPath());
            frame->Show();
        }
        else if (fn.DirExists()) {
            wxDir dir(fn.GetFullPath());
            if (!dir.IsOpened()) return;

            wxString name;
            bool cont = dir.GetFirst(&name, "*", wxDIR_DIRS | wxDIR_FILES);
            vector<wxFileName> folderItems;
            while (cont) {
                folderItems.push_back(wxFileName(fn.GetFullPath(), name));
                cont = dir.GetNext(&name);
            }

            m_items = folderItems;
            UpdateList();
        }
    }

    static wxULongLong GetFolderSize(const wxFileName& folder) {
        wxULongLong total = 0;
        wxDir dir(folder.GetFullPath());
        if (!dir.IsOpened()) return total;

        wxString name;
        bool cont = dir.GetFirst(&name, "*", wxDIR_DIRS | wxDIR_FILES);
        while (cont) {
            wxFileName fn(folder.GetFullPath(), name);
            if (fn.DirExists()) total += GetFolderSize(fn);
            else total += fn.GetSize();
            cont = dir.GetNext(&name);
        }
        return total;
    }

    static wxString FormatSize(wxULongLong size) {
        double sz = static_cast<double>(size.GetValue());
        const char* units[] = { "B", "KB", "MB", "GB", "TB" };
        int u = 0;
        while (sz >= 1024.0 && u < 4) { sz /= 1024.0; u++; }
        return wxString::Format("%.2f %s", sz, units[u]);
    }
};

class MyApp : public wxApp {
public:
    bool OnInit() override {
        wxInitAllImageHandlers();

        wxFrame* frame = new wxFrame(nullptr, wxID_ANY, "File Browser", wxDefaultPosition, wxSize(800, 500));
        new FileBrowser(frame);
        frame->Show();
        return true;
    }
};

wxIMPLEMENT_APP(MyApp);
