#ifndef COURSE_DATABASE_H
#define COURSE_DATABASE_H

#include <mutex>
#include <string>
#include <vector>

struct Course {
    std::string courseCode;
    std::string courseTitle;
    std::string section;
    std::string instructor;
    std::string day;
    std::string startTime;
    std::string endTime;
    std::string classroom;
    std::string semester;
};

class CourseDatabase {
public:
    explicit CourseDatabase(std::string filePath);

    bool load(std::string& message);
    bool addCourse(const Course& course, std::string& message);
    bool updateCourse(const std::string& courseCode,
                      const std::string& section,
                      const std::string& fieldName,
                      const std::string& newValue,
                      std::string& message);
    bool updateCourseByCodeIfUnique(const std::string& courseCode,
                                     const std::string& fieldName,
                                     const std::string& newValue,
                                     std::string& message);
    bool deleteCourse(const std::string& courseCode,
                      const std::string& section,
                      std::string& message);

    std::vector<Course> queryByCode(const std::string& courseCode) const;
    std::vector<Course> queryByInstructor(const std::string& instructor) const;
    std::vector<Course> queryBySemester(const std::string& semester) const;
    std::vector<Course> queryByTimeSlot(const std::string& day,
                                        const std::string& startTime,
                                        const std::string& endTime) const;

    std::string formatCourses(const std::vector<Course>& courses) const;

    static bool parseCoursePayload(const std::string& payload,
                                   Course& course,
                                   std::string& message);
    static bool isUpdatableField(const std::string& fieldName);
    static bool isValidTime(const std::string& timeText);

private:
    std::string filePath_;
    mutable std::mutex mutex_;
    std::vector<Course> courses_;

    bool loadUnlocked(std::string& message);
    bool saveUnlocked(std::string& message) const;
    bool validateCourse(const Course& course, std::string& message) const;
};

#endif  // COURSE_DATABASE_H
