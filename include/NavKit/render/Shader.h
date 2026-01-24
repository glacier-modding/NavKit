#pragma once

#include <string>
#include <sstream>
#include <glm/fwd.hpp>

class Shader {
public:
    unsigned int ID;

    Shader();

    void loadShaders(const char* vertexPath, const char* fragmentPath);

    void use() const;

    void setBool(const std::string& name, bool value) const;

    void setInt(const std::string& name, int value) const;

    void setFloat(const std::string& name, float value) const;

    void setVec4(const std::string& name, const glm::vec4& value) const;

    void setMat3(const std::string& name, const glm::mat3& mat) const;

    void setMat4(const std::string& name, const glm::mat4& mat) const;
};
