#include "cli/message_format.h"

#include "messages.pb.h"

#include <algorithm>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace mm {
namespace {

const std::vector<std::string>& supported_types() {
    static const std::vector<std::string> types = {
        "mm.StringMsg",
        "mm.Point3D",
        "mm.PointCloud",
    };
    return types;
}

FormatResult parse_error(const std::string& type_name) {
    FormatResult result;
    result.error = "failed to parse " + type_name;
    return result;
}

FormatResult unsupported_error(const std::string& type_name) {
    FormatResult result;
    result.error = "unsupported message type: " + type_name;
    return result;
}

FormatResult ok_result(std::string text) {
    FormatResult result;
    result.ok = true;
    result.text = std::move(text);
    return result;
}

}  // namespace

bool is_supported_message_type(const std::string& type_name) {
    const auto& types = supported_types();
    return std::find(types.begin(), types.end(), type_name) != types.end();
}

std::vector<std::string> supported_message_types() {
    return supported_types();
}

FormatResult format_message(const std::string& type_name, const std::string& bytes) {
    if (type_name == "mm.StringMsg") {
        StringMsg msg;
        if (!msg.ParseFromString(bytes)) {
            return parse_error(type_name);
        }
        return ok_result("data: " + msg.data());
    }

    if (type_name == "mm.Point3D") {
        Point3D msg;
        if (!msg.ParseFromString(bytes)) {
            return parse_error(type_name);
        }

        std::ostringstream out;
        out << "x: " << msg.x() << '\n'
            << "y: " << msg.y() << '\n'
            << "z: " << msg.z();
        return ok_result(out.str());
    }

    if (type_name == "mm.PointCloud") {
        PointCloud msg;
        if (!msg.ParseFromString(bytes)) {
            return parse_error(type_name);
        }

        std::ostringstream out;
        out << "timestamp: " << msg.timestamp() << '\n'
            << "frame_id: " << msg.frame_id() << '\n'
            << "points: " << msg.points_size();
        return ok_result(out.str());
    }

    return unsupported_error(type_name);
}

}  // namespace mm
