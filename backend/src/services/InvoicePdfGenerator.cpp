#include "services/InvoicePdfGenerator.h"

#include <hpdf.h>

#include <cmath>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace vrms {
namespace {

void pdfErrorHandler(HPDF_STATUS error, HPDF_STATUS detail, void* context) {
    auto* lastError = static_cast<HPDF_STATUS*>(context);
    if (lastError) *lastError = error ? error : detail;
}

void checkStatus(HPDF_STATUS status, const char* operation) {
    if (status != HPDF_OK) {
        throw std::runtime_error(std::string("PDF generation failed during ") + operation);
    }
}

struct PdfDocDeleter {
    void operator()(HPDF_Doc doc) const { if (doc) HPDF_Free(doc); }
};

std::string safeText(const std::string& value) {
    std::string result;
    result.reserve(value.size());
    for (const unsigned char c : value) {
        if (c == '\n' || c == '\r' || c == '\t') result.push_back(' ');
        else if (c < 0x20 || c >= 0x80) result.push_back('?');
        else result.push_back(static_cast<char>(c));
    }
    return result;
}

std::string money(double value) {
    std::ostringstream out;
    out << "INR " << std::fixed << std::setprecision(2) << value;
    return out.str();
}

void drawText(HPDF_Page page, HPDF_Font font, float size, float x, float y,
              const std::string& text, float r = 0.12f,
              float g = 0.20f, float b = 0.17f) {
    checkStatus(HPDF_Page_BeginText(page), "starting text");
    checkStatus(HPDF_Page_SetFontAndSize(page, font, size), "setting font");
    checkStatus(HPDF_Page_SetRGBFill(page, r, g, b), "setting text color");
    checkStatus(HPDF_Page_TextOut(page, x, y, safeText(text).c_str()), "writing text");
    checkStatus(HPDF_Page_EndText(page), "finishing text");
}

void drawLine(HPDF_Page page, float x1, float y1, float x2, float y2,
              float r = 0.84f, float g = 0.87f, float b = 0.84f) {
    checkStatus(HPDF_Page_SetRGBStroke(page, r, g, b), "setting line color");
    checkStatus(HPDF_Page_SetLineWidth(page, 0.8f), "setting line width");
    checkStatus(HPDF_Page_MoveTo(page, x1, y1), "drawing line");
    checkStatus(HPDF_Page_LineTo(page, x2, y2), "drawing line");
    checkStatus(HPDF_Page_Stroke(page), "drawing line");
}

void drawRightText(HPDF_Page page, HPDF_Font font, float size, float right,
                   float y, const std::string& text, float r = 0.12f,
                   float g = 0.20f, float b = 0.17f) {
    const std::string cleaned = safeText(text);
    checkStatus(HPDF_Page_BeginText(page), "measuring text");
    checkStatus(HPDF_Page_SetFontAndSize(page, font, size), "measuring text");
    const auto width = HPDF_Page_TextWidth(page, cleaned.c_str());
    checkStatus(HPDF_Page_EndText(page), "measuring text");
    drawText(page, font, size, right - width, y, cleaned, r, g, b);
}

} // namespace

std::vector<std::uint8_t> InvoicePdfGenerator::generate(
    const InvoiceDocumentData& invoice
) const {
    if (invoice.invoiceId.empty() || invoice.bookingId.empty() ||
        invoice.customerName.empty() || invoice.customerEmail.empty() ||
        invoice.vehicleId.empty() || invoice.durationDays < 1 ||
        !std::isfinite(invoice.subtotal) || invoice.subtotal <= 0 ||
        !std::isfinite(invoice.tax) || invoice.tax < 0 ||
        !std::isfinite(invoice.total) || invoice.total <= 0) {
        throw std::invalid_argument("Invoice document contains invalid required data");
    }

    HPDF_STATUS lastError = HPDF_OK;
    std::unique_ptr<std::remove_pointer_t<HPDF_Doc>, PdfDocDeleter> pdf(
        HPDF_New(pdfErrorHandler, &lastError)
    );
    if (!pdf) throw std::runtime_error("PDF library could not create a document");
    HPDF_Doc document = pdf.get();

    HPDF_UseUTFEncodings(document);
    checkStatus(HPDF_SetCompressionMode(document, HPDF_COMP_ALL), "enabling compression");
    checkStatus(HPDF_SetInfoAttr(document, HPDF_INFO_TITLE, "Vehicle Rental Invoice"), "setting title");
    checkStatus(HPDF_SetInfoAttr(document, HPDF_INFO_AUTHOR, "Vehicle Rental Management System"), "setting author");

    HPDF_Page page = HPDF_AddPage(document);
    if (!page) throw std::runtime_error("PDF library could not create an invoice page");
    checkStatus(HPDF_Page_SetSize(page, HPDF_PAGE_SIZE_A4, HPDF_PAGE_PORTRAIT), "sizing page");

    const HPDF_Font regular = HPDF_GetFont(document, "Helvetica", "WinAnsiEncoding");
    const HPDF_Font bold = HPDF_GetFont(document, "Helvetica-Bold", "WinAnsiEncoding");
    if (!regular || !bold) throw std::runtime_error("PDF library could not load standard fonts");

    constexpr float pageWidth = 595.0f;
    constexpr float left = 48.0f;
    constexpr float right = 547.0f;

    // Brand header.
    checkStatus(HPDF_Page_SetRGBFill(page, 0.09f, 0.24f, 0.19f), "drawing header");
    checkStatus(HPDF_Page_Rectangle(page, 0.0f, 718.0f, pageWidth, 124.0f), "drawing header");
    checkStatus(HPDF_Page_Fill(page), "drawing header");
    drawText(page, bold, 19.0f, left, 796.0f, "NORTHSTAR MOBILITY", 0.91f, 0.96f, 0.88f);
    drawText(page, regular, 9.0f, left, 778.0f, "VEHICLE RENTAL MANAGEMENT", 0.72f, 0.82f, 0.73f);
    drawRightText(page, bold, 25.0f, right, 789.0f, "INVOICE", 0.91f, 0.96f, 0.88f);
    drawRightText(page, regular, 9.0f, right, 770.0f, "Invoice " + invoice.invoiceId,
                  0.72f, 0.82f, 0.73f);

    drawText(page, bold, 10.0f, left, 684.0f, "BILLED TO");
    drawText(page, bold, 12.0f, left, 665.0f, invoice.customerName);
    drawText(page, regular, 9.0f, left, 649.0f, invoice.customerEmail, 0.38f, 0.45f, 0.40f);
    drawText(page, regular, 9.0f, left, 633.0f, "Customer ID: " + invoice.customerId, 0.38f, 0.45f, 0.40f);

    drawText(page, bold, 9.0f, 350.0f, 684.0f, "BOOKING REFERENCE");
    drawText(page, regular, 10.0f, 350.0f, 665.0f, invoice.bookingId);
    drawText(page, regular, 9.0f, 350.0f, 649.0f, "Payment status: " + invoice.paymentStatus);

    drawLine(page, left, 610.0f, right, 610.0f);
    drawText(page, bold, 9.0f, left, 590.0f, "VEHICLE & RENTAL DETAILS");
    drawText(page, bold, 11.0f, left, 568.0f,
             invoice.vehicleBrand + " " + invoice.vehicleModel + " (" + invoice.vehicleType + ")");
    drawText(page, regular, 9.0f, left, 550.0f, "Vehicle ID: " + invoice.vehicleId);
    drawText(page, regular, 9.0f, left, 532.0f,
             "Rental dates: " + invoice.rentalStartDate + " to " + invoice.rentalEndDate);
    drawRightText(page, bold, 10.0f, right, 568.0f,
                  std::to_string(invoice.durationDays) +
                      (invoice.durationDays == 1 ? " day" : " days"));

    // Itemized amount block.
    drawLine(page, left, 505.0f, right, 505.0f);
    drawText(page, bold, 8.5f, left, 486.0f, "DESCRIPTION");
    drawRightText(page, bold, 8.5f, right, 486.0f, "AMOUNT");
    drawLine(page, left, 475.0f, right, 475.0f);

    drawText(page, regular, 10.0f, left, 453.0f,
             "Vehicle rental - " + invoice.vehicleBrand + " " + invoice.vehicleModel);
    drawText(page, regular, 8.5f, left, 437.0f,
             std::to_string(invoice.durationDays) + " days x " + money(invoice.dailyRate) + " / day",
             0.40f, 0.48f, 0.42f);
    drawRightText(page, regular, 10.0f, right, 453.0f, money(invoice.subtotal));
    drawLine(page, left, 420.0f, right, 420.0f);

    float y = 394.0f;
    if (invoice.discount > 0.0) {
        drawText(page, regular, 9.5f, 360.0f, y, "Discount");
        drawRightText(page, regular, 9.5f, right, y, "- " + money(invoice.discount));
        y -= 22.0f;
    }
    drawText(page, regular, 9.5f, 360.0f, y, "Taxes");
    drawRightText(page, regular, 9.5f, right, y, money(invoice.tax));
    y -= 22.0f;
    drawLine(page, 345.0f, y + 9.0f, right, y + 9.0f);
    drawText(page, bold, 12.0f, 360.0f, y - 12.0f, "TOTAL PAID");
    drawRightText(page, bold, 13.0f, right, y - 12.0f, money(invoice.total));

    // Simulated payment disclosure and support footer.
    drawLine(page, left, 95.0f, right, 95.0f);
    drawText(page, bold, 8.5f, left, 76.0f, "PAYMENT", 0.25f, 0.40f, 0.30f);
    drawText(page, regular, 8.5f, left, 61.0f,
             "Payment status: " + invoice.paymentStatus +
                 " | This project uses simulated payments; no real charge was made.",
             0.38f, 0.45f, 0.40f);
    drawRightText(page, regular, 8.0f, right, 36.0f, "Thank you for choosing Northstar Mobility.");

    checkStatus(HPDF_SaveToStream(document), "saving document");
    const HPDF_UINT32 size = HPDF_GetStreamSize(document);
    if (size < 8) throw std::runtime_error("PDF library generated an empty document");
    std::vector<std::uint8_t> bytes(size);
    HPDF_UINT32 readSize = size;
    checkStatus(HPDF_ReadFromStream(document, bytes.data(), &readSize), "reading document");
    bytes.resize(readSize);
    if (bytes.size() < 5 || std::string(bytes.begin(), bytes.begin() + 5) != "%PDF-")
        throw std::runtime_error("PDF library generated an invalid document");
    return bytes;
}

}
