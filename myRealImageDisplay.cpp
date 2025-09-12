#include <wx/wx.h>
#include <wx/artprov.h>
#include <wx/xrc/xmlres.h>
#include <wx/intl.h>
#include <wx/string.h>
#include <wx/stattext.h>
#include <wx/gdicmn.h>
#include <wx/font.h>
#include <wx/colour.h>
#include <wx/settings.h>
#include <wx/filepicker.h>
#include <wx/sizer.h>
#include <wx/checklst.h>
#include <wx/panel.h>
#include <wx/frame.h>
#include <wx/aui/aui.h>
#include <wx/dir.h>
#include <wx/filename.h>
#include <fstream>
#include <vector>

using namespace std;

// ================================================================
// Constants from your EDF header
// ================================================================
static const int HEADER_OFFSET = 3072;
static const int WIDTH = 2082;
static const int HEIGHT = 2217;
static const int PIXEL_DEPTH = 4; // RGBA (4 bytes per pixel)

// ================================================================
// ImageFrame
// ================================================================
class ImageFrame : public wxFrame
{
public:
    ImageFrame(wxWindow* parent, const wxString& filepath)
        : wxFrame(parent, wxID_ANY, "Image Display Window",
            wxDefaultPosition, wxSize(820, 750))
    {
        m_mgr.SetManagedWindow(this);
        m_mgr.SetFlags(wxAUI_MGR_DEFAULT);

        // Layout
        wxBoxSizer* vbox = new wxBoxSizer(wxVERTICAL);

        m_imagepanel1 = new wxPanel(this, wxID_ANY);
        vbox->Add(m_imagepanel1, 1, wxEXPAND);

        // ✅ Toolbar
        wxToolBar* toolbar = CreateToolBar();
        toolbar->AddTool(wxID_ZOOM_IN, "Zoom In", wxArtProvider::GetBitmap(wxART_PLUS, wxART_TOOLBAR));
        toolbar->AddTool(wxID_ZOOM_OUT, "Zoom Out", wxArtProvider::GetBitmap(wxART_MINUS, wxART_TOOLBAR));
        toolbar->Realize();

        // ✅ Status bar (2 fields: pixel info + zoom level)
        CreateStatusBar(2);
        SetStatusText("Ready", 0);
        SetStatusText("Zoom: 100%", 1);

        SetSizer(vbox);

        LoadImage(filepath);

        // Bind events
        m_imagepanel1->Bind(wxEVT_PAINT, &ImageFrame::OnPaint, this);
        m_imagepanel1->Bind(wxEVT_MOTION, &ImageFrame::OnMouseMove, this);

        Bind(wxEVT_TOOL, &ImageFrame::OnZoomIn, this, wxID_ZOOM_IN);
        Bind(wxEVT_TOOL, &ImageFrame::OnZoomOut, this, wxID_ZOOM_OUT);

        this->Centre();
    }

    ~ImageFrame() { m_mgr.UnInit(); }

private:
    wxPanel* m_imagepanel1;
    wxAuiManager m_mgr;
    wxBitmap m_bitmap;
    wxImage m_originalImg;   // store original image for scaling
    double m_zoomFactor = 1.0;

    void LoadImage(const wxString& filepath)
    {
        std::ifstream file(filepath.mb_str().data(), ios::binary);
        if (!file) return;

        // Skip header
        file.seekg(HEADER_OFFSET, ios::beg);

        // Read raw pixel data
        std::vector<unsigned char> buffer(WIDTH * HEIGHT * PIXEL_DEPTH);
        file.read(reinterpret_cast<char*>(buffer.data()), buffer.size());

        if (file.gcount() < (streamsize)buffer.size())
        {
            wxLogError("File too small or truncated: %s", filepath);
            return;
        }

        // Create wxImage
        wxImage img(WIDTH, HEIGHT);
        unsigned char* rgb = img.GetData();
        img.SetAlpha();
        unsigned char* alpha = img.GetAlpha();

        // Convert to grayscale
        for (int i = 0; i < WIDTH * HEIGHT; i++)
        {
            unsigned char r = buffer[i * 4 + 2];
            unsigned char g = buffer[i * 4 + 1];
            unsigned char b = buffer[i * 4 + 0];
            unsigned char a = buffer[i * 4 + 3];

            unsigned char grey = static_cast<unsigned char>(
                0.299 * r + 0.587 * g + 0.114 * b
                );

            rgb[i * 3 + 0] = grey;
            rgb[i * 3 + 1] = grey;
            rgb[i * 3 + 2] = grey;
            alpha[i] = a;
        }

        // Store original for zooming
        m_originalImg = img;

        // Initial scaled bitmap
        int initW = 800;
        int initH = 600;
        wxImage scaled = img.Scale(initW, initH, wxIMAGE_QUALITY_HIGH);
        m_bitmap = wxBitmap(scaled);

        // ✅ Correctly set zoomFactor to match what’s displayed
        m_zoomFactor = static_cast<double>(initW) / m_originalImg.GetWidth();

        // ✅ Update status bar immediately
        wxString zoomStr;
        zoomStr.Printf("Zoom: %.0f%%", m_zoomFactor * 100);
        SetStatusText(zoomStr, 1);
    }

    void OnZoomIn(wxCommandEvent& event)
    {
        m_zoomFactor *= 1.2; // zoom in 20%
        ApplyZoom();
    }

    void OnZoomOut(wxCommandEvent& event)
    {
        m_zoomFactor /= 1.2; // zoom out 20%
        if (m_zoomFactor < 0.1) m_zoomFactor = 0.1; // minimum size
        ApplyZoom();
    }

    void ApplyZoom()
    {
        int newW = static_cast<int>(m_originalImg.GetWidth() * m_zoomFactor);
        int newH = static_cast<int>(m_originalImg.GetHeight() * m_zoomFactor);

        wxImage scaled = m_originalImg.Scale(newW, newH, wxIMAGE_QUALITY_HIGH);
        m_bitmap = wxBitmap(scaled);

        m_imagepanel1->Refresh();

        // Update status bar
        wxString zoomStr;
        zoomStr.Printf("Zoom: %.0f%%", m_zoomFactor * 100);
        SetStatusText(zoomStr, 1);
    }

    void OnPaint(wxPaintEvent& event)
    {
        wxPaintDC dc(m_imagepanel1);
        if (m_bitmap.IsOk())
            dc.DrawBitmap(m_bitmap, 0, 0, true);
    }

    void OnMouseMove(wxMouseEvent& event)
    {
        if (!m_bitmap.IsOk()) return;

        wxPoint pos = event.GetPosition();
        if (pos.x < 0 || pos.y < 0 || pos.x >= m_bitmap.GetWidth() || pos.y >= m_bitmap.GetHeight())
            return;

        wxImage img = m_bitmap.ConvertToImage();
        unsigned char* rgb = img.GetData();

        int idx = (pos.y * img.GetWidth() + pos.x) * 3;
        unsigned char r = rgb[idx];
        unsigned char g = rgb[idx + 1];
        unsigned char b = rgb[idx + 2];

        int intensity = (r + g + b) / 3;

        wxString msg;
        msg.Printf("x=%d, y=%d, intensity=%d", pos.x, pos.y, intensity);
        SetStatusText(msg, 0);
    }
};

// ================================================================
// FileBrowser
// ================================================================
class FileBrowser : public wxPanel
{
public:
    FileBrowser(wxWindow* parent)
        : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(500, 300))
    {
        wxBoxSizer* vbox = new wxBoxSizer(wxVERTICAL);

        wxBoxSizer* hbox = new wxBoxSizer(wxHORIZONTAL);
        m_staticText1 = new wxStaticText(this, wxID_ANY, "Folder:");
        hbox->Add(m_staticText1, 0, wxALL, 5);

        m_dirPicker1 = new wxDirPickerCtrl(this, wxID_ANY);
        hbox->Add(m_dirPicker1, 1, wxALL | wxEXPAND, 5);

        vbox->Add(hbox, 0, wxEXPAND, 5);

        m_checkList1 = new wxCheckListBox(this, wxID_ANY);
        vbox->Add(m_checkList1, 1, wxALL | wxEXPAND, 5);

        this->SetSizer(vbox);

        // Bind events
        m_dirPicker1->Bind(wxEVT_DIRPICKER_CHANGED, &FileBrowser::OnDirChanged, this);
        m_checkList1->Bind(wxEVT_LISTBOX, &FileBrowser::OnFileSelected, this);
    }

private:
    wxStaticText* m_staticText1;
    wxDirPickerCtrl* m_dirPicker1;
    wxCheckListBox* m_checkList1;

    void OnDirChanged(wxFileDirPickerEvent& event)
    {
        wxString folder = event.GetPath();
        m_checkList1->Clear();

        wxDir dir(folder);
        if (!dir.IsOpened()) return;

        wxString filename;
        bool cont = dir.GetFirst(&filename, "*", wxDIR_FILES | wxDIR_HIDDEN);
        while (cont)
        {
            wxFileName fn(folder, filename);
            m_checkList1->Append(fn.GetFullPath());
            cont = dir.GetNext(&filename);
        }
    }

    void OnFileSelected(wxCommandEvent& event)
    {
        wxString filepath = m_checkList1->GetStringSelection();
        if (!filepath.IsEmpty())
        {
            auto* frame = new ImageFrame(nullptr, filepath);
            frame->Show();
        }
    }
};

// ================================================================
// Application
// ================================================================
class MyApp : public wxApp
{
public:
    bool OnInit() override
    {
        wxFrame* frame = new wxFrame(nullptr, wxID_ANY, "File Browser",
            wxDefaultPosition, wxSize(600, 400));
        new FileBrowser(frame);
        frame->Show();
        return true;
    }
};

wxIMPLEMENT_APP(MyApp);