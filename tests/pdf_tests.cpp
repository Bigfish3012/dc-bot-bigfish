#include "../include/runtime_limits.h"
#include <cassert>
#include <future>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

std::string extract_text_from_pdf(const std::string& content);

// Small valid PDF generated in memory; distinct content detects temporary-file collisions.
std::string pdf(const std::string& word) {
    const std::string stream = "BT /F1 12 Tf 40 100 Td (" + word + ") Tj ET\n";
    const std::vector<std::string> objects = {
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 300] /Resources << /Font << /F1 4 0 R >> >> /Contents 5 0 R >>",
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
        "<< /Length " + std::to_string(stream.size()) + " >>\nstream\n" + stream + "endstream"
    };
    std::ostringstream out;
    out << "%PDF-1.4\n";
    std::vector<std::streamoff> offsets{0};
    for (std::size_t i = 0; i < objects.size(); ++i) {
        offsets.push_back(out.tellp());
        out << i + 1 << " 0 obj\n" << objects[i] << "\nendobj\n";
    }
    const auto xref = out.tellp();
    out << "xref\n0 6\n0000000000 65535 f \n";
    for (std::size_t i = 1; i < offsets.size(); ++i) {
        out << std::setw(10) << std::setfill('0') << offsets[i] << " 00000 n \n";
    }
    out << "trailer\n<< /Size 6 /Root 1 0 R >>\nstartxref\n" << xref << "\n%%EOF\n";
    return out.str();
}

int main() {
    assert(extract_text_from_pdf({}).empty());
    assert(extract_text_from_pdf(std::string(limits::attachment_bytes + 1, 'x')).empty());
    assert(extract_text_from_pdf("invalid PDF").empty());
    std::vector<std::future<void>> tasks;
    for (int i = 0; i < 8; ++i) {
        tasks.push_back(std::async(std::launch::async, [i] {
            const auto word = "session" + std::to_string(i);
            const auto text = extract_text_from_pdf(pdf(word));
            assert(text.find(word) != std::string::npos);
            for (int other = 0; other < 8; ++other) {
                if (other != i) assert(text.find("session" + std::to_string(other)) == std::string::npos);
            }
        }));
    }
    for (auto& task : tasks) task.get();
    std::cout << "PASS: PDF invalid/oversize input and eight concurrent independent extractions\n";
}
