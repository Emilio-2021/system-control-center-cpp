#include "http_response.h"

#include <sstream>

std::string response(std::string_view status, std::string_view type,
                     std::string_view body, std::string_view extra_headers) {
    std::string rendered_body(body);
    const std::string plain_brand = R"(<a class="navbar-brand" href="/dashboard">System Control Center</a>)";
    const std::string arrow_brand = R"(<a class="navbar-brand" href="/dashboard">â† System Control Center</a>)";
    std::string section_label = "System Control Center";
    if (rendered_body.find("Product Master Catalog") != std::string::npos) section_label = "Stock Room Registry";
    else if (rendered_body.find("Business Entities Registry") != std::string::npos) section_label = "Enterprise Business Entities Matrix";
    else if (rendered_body.find("Users and Roles") != std::string::npos) section_label = "User Records Management Grid";
    else if (rendered_body.find("Orders &amp; Invoiced Line Items") != std::string::npos) section_label = "Transaction Tracking Room";
    else if (rendered_body.find("Invoice Checkout Wizard") != std::string::npos || rendered_body.find("Create New Sales Order Invoice") != std::string::npos) section_label = "Sales Register Desk";
    else if (rendered_body.find("Order #") != std::string::npos) section_label = "Order Details";
    const std::string dashboard_brand = "<div class=\"d-flex align-items-center\"><a class=\"navbar-brand\" href=\"/dashboard\">← System Dashboard</a><span class=\"navbar-text text-white-50 ms-3\">" + section_label + "</span></div>";
    std::size_t brand_position = 0;
    while ((brand_position = rendered_body.find(plain_brand, brand_position)) != std::string::npos) {
        rendered_body.replace(brand_position, plain_brand.size(), dashboard_brand);
        brand_position += dashboard_brand.size();
    }
    brand_position = 0;
    while ((brand_position = rendered_body.find(arrow_brand, brand_position)) != std::string::npos) {
        rendered_body.replace(brand_position, arrow_brand.size(), dashboard_brand);
        brand_position += dashboard_brand.size();
    }
    std::ostringstream output;
    output << "HTTP/1.1 " << status << "\r\n"
           << "Content-Type: " << type << "\r\n"
           << extra_headers
           << "Content-Length: " << rendered_body.size() << "\r\n"
           << "Connection: close\r\n\r\n";
    output.write(rendered_body.data(), static_cast<std::streamsize>(rendered_body.size()));
    return output.str();
}

std::string redirect(std::string_view location) {
    std::ostringstream output;
    output << "HTTP/1.1 303 See Other\r\n"
           << "Location: " << location << "\r\n"
           << "Content-Length: 0\r\n"
           << "Connection: close\r\n\r\n";
    return output.str();
}
