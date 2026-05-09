#include "CourseDatabase.h"

#include "Protocol.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <utility>

namespace {

std::string normalizedDay(const std::string& day) {
    const std::string upper = Protocol::toUpper(Protocol::trim(day));
    if (upper.size() >= 3) {
        return upper.substr(0, 3);
    }
    return upper;
}

std::string normalizedCode(const std::string& value) {
    return Protocol::toUpper(Protocol::trim(value));
}

bool containsIgnoreCase(const std::string& text, const std::string& term) {
    return Protocol::toUpper(text).find(Protocol::toUpper(term)) != std::string::npos;
}

std::vector<std::string> parseCsvLine(const std::string& line) {
    std::vector<std::string> fields;
    std::string current;
    bool inQuotes = false;

    for (std::size_t i = 0; i < line.size(); ++i) {
        const char ch = line[i];
        if (ch == '"') {
            if (inQuotes && i + 1 < line.size() && line[i + 1] == '"') {
                current.push_back('"');
                ++i;
            } else {
                inQuotes = !inQuotes;
            }
        } else if (ch == ',' && !inQuotes) {
            fields.push_back(Protocol::trim(current));
            current.clear();
        } else {
            current.push_back(ch);
        }
    }

    fields.push_back(Protocol::trim(current));
    return fields;
}

std::string csvEscape(const std::string& field) {
    const bool needsQuotes = field.find_first_of(",\"\r\n") != std::string::npos;
    if (!needsQuotes) {
        return field;
    }

    std::string escaped = "\"";
    for (const char ch : field) {
        if (ch == '"') {
            escaped += "\"\"";
        } else {
            escaped.push_back(ch);
        }
    }
    escaped.push_back('"');
    return escaped;
}

Course courseFromFields(const std::vector<std::string>& fields) {
    Course course;
    course.courseCode = fields[0];
    course.courseTitle = fields[1];
    course.section = fields[2];
    course.instructor = fields[3];
    course.day = fields[4];
    course.startTime = fields[5];
    course.endTime = fields[6];
    course.classroom = fields[7];
    course.semester = fields[8];
    return course;
}

std::string courseToCsv(const Course& course) {
    std::ostringstream output;
    output << csvEscape(course.courseCode) << ','
           << csvEscape(course.courseTitle) << ','
           << csvEscape(course.section) << ','
           << csvEscape(course.instructor) << ','
           << csvEscape(course.day) << ','
           << csvEscape(course.startTime) << ','
           << csvEscape(course.endTime) << ','
           << csvEscape(course.classroom) << ','
           << csvEscape(course.semester);
    return output.str();
}

int parseMinutes(const std::string& timeText) {
    if (!CourseDatabase::isValidTime(timeText)) {
        return -1;
    }

    const int hours = std::stoi(timeText.substr(0, 2));
    const int minutes = std::stoi(timeText.substr(3, 2));
    return hours * 60 + minutes;
}

bool timeSlotsOverlap(const std::string& courseStart,
                      const std::string& courseEnd,
                      const std::string& queryStart,
                      const std::string& queryEnd) {
    const int cStart = parseMinutes(courseStart);
    const int cEnd = parseMinutes(courseEnd);
    const int qStart = parseMinutes(queryStart);
    const int qEnd = parseMinutes(queryEnd);
    if (cStart < 0 || cEnd < 0 || qStart < 0 || qEnd < 0) {
        return false;
    }
    return cStart < qEnd && qStart < cEnd;
}

std::string minutesToTime(int minutes) {
    const int hours = minutes / 60;
    const int mins = minutes % 60;
    std::ostringstream output;
    output << std::setw(2) << std::setfill('0') << hours
           << ':' << std::setw(2) << std::setfill('0') << mins;
    return output.str();
}

bool applyTimeUpdate(Course& course, const std::string& value, std::string& message) {
    std::string normalized = Protocol::trim(value);
    std::replace(normalized.begin(), normalized.end(), '-', ' ');

    std::istringstream input(normalized);
    std::string day;
    std::string start;
    std::string end;
    input >> day >> start >> end;

    if (day.empty() || start.empty()) {
        message = "ERROR TIME update requires Day StartTime [EndTime].";
        return false;
    }

    if (!CourseDatabase::isValidTime(start)) {
        message = "ERROR StartTime must use HH:MM format.";
        return false;
    }

    if (end.empty()) {
        const int oldStart = parseMinutes(course.startTime);
        const int oldEnd = parseMinutes(course.endTime);
        const int duration = oldEnd - oldStart;
        const int newStart = parseMinutes(start);
        const int newEnd = newStart + duration;

        if (duration <= 0 || newEnd > 23 * 60 + 59) {
            message = "ERROR Cannot preserve the old duration for this TIME update.";
            return false;
        }
        end = minutesToTime(newEnd);
    }

    if (!CourseDatabase::isValidTime(end)) {
        message = "ERROR EndTime must use HH:MM format.";
        return false;
    }

    if (parseMinutes(start) >= parseMinutes(end)) {
        message = "ERROR StartTime must be earlier than EndTime.";
        return false;
    }

    course.day = day;
    course.startTime = start;
    course.endTime = end;
    message = "OK Course time updated.";
    return true;
}

std::string fit(const std::string& text, std::size_t width) {
    if (text.size() <= width) {
        return text;
    }
    if (width <= 2) {
        return text.substr(0, width);
    }
    return text.substr(0, width - 2) + "..";
}

bool applyFieldUpdate(Course& course,
                      const std::string& fieldName,
                      const std::string& newValue,
                      std::string& message) {
    const std::string field = Protocol::toUpper(Protocol::trim(fieldName));
    const std::string value = Protocol::trim(newValue);

    if (value.empty()) {
        message = "ERROR New value cannot be empty.";
        return false;
    }

    if (field == "TIME") {
        return applyTimeUpdate(course, value, message);
    }

    if ((field == "STARTTIME" || field == "ENDTIME") &&
        !CourseDatabase::isValidTime(value)) {
        message = "ERROR Time must use HH:MM format.";
        return false;
    }

    if (field == "COURSETITLE" || field == "TITLE") {
        course.courseTitle = value;
    } else if (field == "INSTRUCTOR") {
        course.instructor = value;
    } else if (field == "DAY") {
        course.day = value;
    } else if (field == "STARTTIME" || field == "START") {
        course.startTime = value;
    } else if (field == "ENDTIME" || field == "END") {
        course.endTime = value;
    } else if (field == "CLASSROOM" || field == "ROOM") {
        course.classroom = value;
    } else if (field == "SEMESTER") {
        course.semester = value;
    } else {
        message = "ERROR Unsupported field. Use CourseTitle, Instructor, Day, StartTime, EndTime, Classroom, or Semester.";
        return false;
    }

    if (parseMinutes(course.startTime) >= parseMinutes(course.endTime)) {
        message = "ERROR StartTime must be earlier than EndTime.";
        return false;
    }

    message = "OK Course updated.";
    return true;
}

}  // namespace

CourseDatabase::CourseDatabase(std::string filePath)
    : filePath_(std::move(filePath)) {
}

bool CourseDatabase::load(std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    return loadUnlocked(message);
}

bool CourseDatabase::loadUnlocked(std::string& message) {
    courses_.clear();

    std::ifstream input(filePath_);
    if (!input) {
        std::ofstream create(filePath_);
        if (!create) {
            message = "ERROR Could not create database file: " + filePath_;
            return false;
        }
        create << "CourseCode,CourseTitle,Section,Instructor,Day,StartTime,EndTime,Classroom,Semester\n";
        message = "OK Created empty database file.";
        return true;
    }

    std::string line;
    bool firstLine = true;
    int skippedLines = 0;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (firstLine) {
            firstLine = false;
            continue;
        }

        if (Protocol::trim(line).empty()) {
            continue;
        }

        const std::vector<std::string> fields = parseCsvLine(line);
        if (fields.size() != 9) {
            ++skippedLines;
            continue;
        }

        courses_.push_back(courseFromFields(fields));
    }

    if (skippedLines > 0) {
        message = "OK Loaded database with " + std::to_string(skippedLines) + " malformed line(s) skipped.";
    } else {
        message = "OK Loaded database.";
    }
    return true;
}

bool CourseDatabase::saveUnlocked(std::string& message) const {
    std::ofstream output(filePath_, std::ios::trunc);
    if (!output) {
        message = "ERROR Could not write database file.";
        return false;
    }

    output << "CourseCode,CourseTitle,Section,Instructor,Day,StartTime,EndTime,Classroom,Semester\n";
    for (const Course& course : courses_) {
        output << courseToCsv(course) << '\n';
    }

    message = "OK Database saved.";
    return true;
}

bool CourseDatabase::validateCourse(const Course& course, std::string& message) const {
    if (Protocol::trim(course.courseCode).empty() ||
        Protocol::trim(course.courseTitle).empty() ||
        Protocol::trim(course.section).empty() ||
        Protocol::trim(course.instructor).empty() ||
        Protocol::trim(course.day).empty() ||
        Protocol::trim(course.startTime).empty() ||
        Protocol::trim(course.endTime).empty() ||
        Protocol::trim(course.classroom).empty() ||
        Protocol::trim(course.semester).empty()) {
        message = "ERROR All course fields are required.";
        return false;
    }

    if (!isValidTime(course.startTime) || !isValidTime(course.endTime)) {
        message = "ERROR StartTime and EndTime must use HH:MM format.";
        return false;
    }

    if (parseMinutes(course.startTime) >= parseMinutes(course.endTime)) {
        message = "ERROR StartTime must be earlier than EndTime.";
        return false;
    }

    return true;
}

bool CourseDatabase::addCourse(const Course& course, std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!validateCourse(course, message)) {
        return false;
    }

    const std::string newCode = normalizedCode(course.courseCode);
    const std::string newSection = Protocol::toUpper(Protocol::trim(course.section));
    for (const Course& existing : courses_) {
        if (normalizedCode(existing.courseCode) == newCode &&
            Protocol::toUpper(Protocol::trim(existing.section)) == newSection) {
            message = "ERROR Course code and section already exist.";
            return false;
        }
    }

    courses_.push_back(course);
    if (!saveUnlocked(message)) {
        return false;
    }

    message = "OK Course added.";
    return true;
}

bool CourseDatabase::updateCourse(const std::string& courseCode,
                                  const std::string& section,
                                  const std::string& fieldName,
                                  const std::string& newValue,
                                  std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);

    const std::string targetCode = normalizedCode(courseCode);
    const std::string targetSection = Protocol::toUpper(Protocol::trim(section));

    for (Course& course : courses_) {
        if (normalizedCode(course.courseCode) == targetCode &&
            Protocol::toUpper(Protocol::trim(course.section)) == targetSection) {
            Course updated = course;
            if (!applyFieldUpdate(updated, fieldName, newValue, message)) {
                return false;
            }
            course = updated;
            if (!saveUnlocked(message)) {
                return false;
            }
            message = "OK Course updated.";
            return true;
        }
    }

    message = "ERROR Course code and section not found.";
    return false;
}

bool CourseDatabase::updateCourseByCodeIfUnique(const std::string& courseCode,
                                                const std::string& fieldName,
                                                const std::string& newValue,
                                                std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);

    const std::string targetCode = normalizedCode(courseCode);
    std::vector<std::size_t> matches;
    for (std::size_t i = 0; i < courses_.size(); ++i) {
        if (normalizedCode(courses_[i].courseCode) == targetCode) {
            matches.push_back(i);
        }
    }

    if (matches.empty()) {
        message = "ERROR Course code not found.";
        return false;
    }

    if (matches.size() > 1) {
        message = "ERROR Multiple sections found. Use UPDATE CourseCode Section Field NewValue.";
        return false;
    }

    Course updated = courses_[matches[0]];
    if (!applyFieldUpdate(updated, fieldName, newValue, message)) {
        return false;
    }

    courses_[matches[0]] = updated;
    if (!saveUnlocked(message)) {
        return false;
    }

    message = "OK Course updated.";
    return true;
}

bool CourseDatabase::deleteCourse(const std::string& courseCode,
                                  const std::string& section,
                                  std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);

    const std::string targetCode = normalizedCode(courseCode);
    const std::string targetSection = Protocol::toUpper(Protocol::trim(section));
    const auto oldSize = courses_.size();

    courses_.erase(std::remove_if(courses_.begin(), courses_.end(),
        [&](const Course& course) {
            return normalizedCode(course.courseCode) == targetCode &&
                   Protocol::toUpper(Protocol::trim(course.section)) == targetSection;
        }), courses_.end());

    if (courses_.size() == oldSize) {
        message = "ERROR Course code and section not found.";
        return false;
    }

    if (!saveUnlocked(message)) {
        return false;
    }

    message = "OK Course deleted.";
    return true;
}

bool CourseDatabase::findCourse(const std::string& courseCode,
                                const std::string& section,
                                const std::string& semester,
                                Course& course) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string targetCode = normalizedCode(courseCode);
    const std::string targetSection = Protocol::toUpper(Protocol::trim(section));
    const std::string targetSemester = Protocol::toUpper(Protocol::trim(semester));
    const bool useSemester = !targetSemester.empty();

    for (const Course& existing : courses_) {
        if (normalizedCode(existing.courseCode) == targetCode &&
            Protocol::toUpper(Protocol::trim(existing.section)) == targetSection &&
            (!useSemester ||
             Protocol::toUpper(Protocol::trim(existing.semester)) == targetSemester)) {
            course = existing;
            return true;
        }
    }

    return false;
}

std::vector<Course> CourseDatabase::queryByCode(const std::string& courseCode) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Course> results;
    const std::string targetCode = normalizedCode(courseCode);

    for (const Course& course : courses_) {
        if (normalizedCode(course.courseCode) == targetCode) {
            results.push_back(course);
        }
    }
    return results;
}

std::vector<Course> CourseDatabase::queryByInstructor(const std::string& instructor) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Course> results;
    const std::string term = Protocol::trim(instructor);

    for (const Course& course : courses_) {
        if (containsIgnoreCase(course.instructor, term)) {
            results.push_back(course);
        }
    }
    return results;
}

std::vector<Course> CourseDatabase::queryBySemester(const std::string& semester) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Course> results;
    const std::string targetSemester = Protocol::toUpper(Protocol::trim(semester));

    for (const Course& course : courses_) {
        if (Protocol::toUpper(Protocol::trim(course.semester)) == targetSemester) {
            results.push_back(course);
        }
    }
    return results;
}

std::vector<Course> CourseDatabase::queryByTimeSlot(const std::string& day,
                                                    const std::string& startTime,
                                                    const std::string& endTime) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Course> results;
    const std::string targetDay = normalizedDay(day);

    for (const Course& course : courses_) {
        if (normalizedDay(course.day) == targetDay &&
            timeSlotsOverlap(course.startTime, course.endTime, startTime, endTime)) {
            results.push_back(course);
        }
    }
    return results;
}

std::vector<Course> CourseDatabase::queryByFilters(const std::string& courseCode,
                                                   const std::string& instructor,
                                                   const std::string& semester,
                                                   const std::string& day,
                                                   const std::string& startTime,
                                                   const std::string& endTime) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Course> results;

    const std::string codeTerm = Protocol::trim(courseCode);
    const std::string instructorTerm = Protocol::trim(instructor);
    const std::string targetSemester = Protocol::toUpper(Protocol::trim(semester));
    const std::string targetDay = normalizedDay(day);
    const int targetStart = parseMinutes(startTime);
    const int targetEnd = parseMinutes(endTime);

    const bool useCode = !codeTerm.empty();
    const bool useInstructor = !instructorTerm.empty();
    const bool useSemester = !targetSemester.empty();
    const bool useDay = !targetDay.empty();
    const bool useStart = targetStart >= 0;
    const bool useEnd = targetEnd >= 0;

    for (const Course& course : courses_) {
        if (useCode && !containsIgnoreCase(course.courseCode, codeTerm)) {
            continue;
        }

        if (useInstructor && !containsIgnoreCase(course.instructor, instructorTerm)) {
            continue;
        }

        if (useSemester &&
            Protocol::toUpper(Protocol::trim(course.semester)) != targetSemester) {
            continue;
        }

        if (useDay && normalizedDay(course.day) != targetDay) {
            continue;
        }

        const int courseStart = parseMinutes(course.startTime);
        const int courseEnd = parseMinutes(course.endTime);
        if (useStart && useEnd) {
            if (!timeSlotsOverlap(course.startTime, course.endTime, startTime, endTime)) {
                continue;
            }
        } else if (useStart) {
            if (courseEnd < 0 || courseEnd <= targetStart) {
                continue;
            }
        } else if (useEnd) {
            if (courseStart < 0 || courseStart >= targetEnd) {
                continue;
            }
        }

        results.push_back(course);
    }

    return results;
}

std::string CourseDatabase::formatCourses(const std::vector<Course>& courses) const {
    std::ostringstream output;

    if (courses.empty()) {
        output << "No matching courses.\n";
        return output.str();
    }

    output << std::left
           << std::setw(12) << "CourseCode"
           << std::setw(30) << "CourseTitle"
           << std::setw(9) << "Section"
           << std::setw(20) << "Instructor"
           << std::setw(8) << "Day"
           << std::setw(10) << "Start"
           << std::setw(10) << "End"
           << std::setw(12) << "Classroom"
           << std::setw(10) << "Semester" << '\n';
    output << std::string(121, '-') << '\n';

    for (const Course& course : courses) {
        output << std::left
               << std::setw(12) << fit(course.courseCode, 11)
               << std::setw(30) << fit(course.courseTitle, 29)
               << std::setw(9) << fit(course.section, 8)
               << std::setw(20) << fit(course.instructor, 19)
               << std::setw(8) << fit(course.day, 7)
               << std::setw(10) << fit(course.startTime, 9)
               << std::setw(10) << fit(course.endTime, 9)
               << std::setw(12) << fit(course.classroom, 11)
               << std::setw(10) << fit(course.semester, 9) << '\n';
    }

    return output.str();
}

bool CourseDatabase::parseCoursePayload(const std::string& payload,
                                        Course& course,
                                        std::string& message) {
    const std::vector<std::string> fields = Protocol::splitByChar(payload, '|');
    if (fields.size() != 9) {
        message = "ERROR ADD requires 9 fields separated by '|'.";
        return false;
    }

    course = courseFromFields(fields);
    message = "OK Course payload parsed.";
    return true;
}

bool CourseDatabase::isUpdatableField(const std::string& fieldName) {
    static const std::vector<std::string> fields = {
        "COURSETITLE", "TITLE", "INSTRUCTOR", "DAY",
        "STARTTIME", "START", "ENDTIME", "END",
        "TIME", "CLASSROOM", "ROOM", "SEMESTER"
    };

    const std::string normalized = Protocol::toUpper(Protocol::trim(fieldName));
    return std::find(fields.begin(), fields.end(), normalized) != fields.end();
}

bool CourseDatabase::isValidTime(const std::string& timeText) {
    if (timeText.size() != 5 || timeText[2] != ':') {
        return false;
    }

    if (!std::isdigit(static_cast<unsigned char>(timeText[0])) ||
        !std::isdigit(static_cast<unsigned char>(timeText[1])) ||
        !std::isdigit(static_cast<unsigned char>(timeText[3])) ||
        !std::isdigit(static_cast<unsigned char>(timeText[4]))) {
        return false;
    }

    const int hours = std::stoi(timeText.substr(0, 2));
    const int minutes = std::stoi(timeText.substr(3, 2));
    return hours >= 0 && hours <= 23 && minutes >= 0 && minutes <= 59;
}
