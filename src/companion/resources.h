// OREGON.GXL archive reader, screen-script (CTR) image lists and the
// label tables for image-only buttons.
#pragma once
#include <map>
#include <string>
#include <vector>

class Gxl {
public:
    bool open(const std::string& path);
    bool get(const std::string& name, std::string& data) const;
    bool isOpen() const { return !entries_.empty(); }
private:
    struct Entry { unsigned off, size; };
    std::map<std::string, Entry> entries_;
    std::string path_;
};

// image names listed by a CTR file (opcode 1), in order
std::vector<std::string> ctrImageList(const std::string& ctrText);

std::string upper(std::string s);
std::string trim(const std::string& s);

// label for button image <imageName> frame <frame>; empty if unknown
std::string imageFrameLabel(const std::string& imageName, int frame);
// per-screen override label for control id (icon buttons etc.)
std::string screenControlLabel(const std::string& ctr, int id, int imageFrame, const std::string& imageName);
// keyboard accelerator for a control on a given screen (0 = none)
char screenAccelerator(const std::string& ctr, int id);
// friendly name for a screen script, empty when not interesting
std::string screenTitle(const std::string& ctr);
