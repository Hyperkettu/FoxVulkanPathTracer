#pragma once

namespace Fox {

    namespace Graphics {

        namespace Vulkan {

            class Camera
            {
            public:
                Camera(float fovDegrees, float aspectRatio, float nearPlane, float farPlane)
                    : fov(glm::radians(fovDegrees)),
                    aspect(aspectRatio),
                    near(nearPlane),
                    far(farPlane)
                {
                    UpdateViewMatrix();
                    UpdateProjectionMatrix();
                }

                // Movement controls
                void MoveForward(float delta)
                {
                    position += front * delta;
                    UpdateViewMatrix();
                }

                void MoveBackward(float delta)
                {
                    position -= front * delta;
                    UpdateViewMatrix();
                }

                void MoveRight(float delta)
                {
                    position += right * delta;
                    UpdateViewMatrix();
                }

                void MoveLeft(float delta)
                {
                    position -= right * delta;
                    UpdateViewMatrix();
                }

                void MoveUp(float delta)
                {
                    position += worldUp * delta;
                    UpdateViewMatrix();
                }

                void MoveDown(float delta)
                {
                    position -= worldUp * delta;
                    UpdateViewMatrix();
                }

                void Rotate(float yawDelta, float pitchDelta)
                {
                    yaw += yawDelta;
                    pitch += pitchDelta;

                    if (pitch > 89.0f) pitch = 89.0f;
                    if (pitch < -89.0f) pitch = -89.0f;

                    UpdateVectors();
                    UpdateViewMatrix();
                }

                void SetPosition(const glm::vec3& pos)
                {
                    position = pos;
                    UpdateViewMatrix();
                }

                void SetAspectRatio(float aspect)
                {
                    this->aspect = aspect;
                    UpdateProjectionMatrix();
                }

                void SetWorldUp(const glm::vec3& upVector)
                {
                    worldUp = upVector;
                    UpdateVectors();
                    UpdateViewMatrix();
				}

                void SetCameraTarget(const glm::vec3& target)
                {
                    front = glm::normalize(target - position);
                    yaw = glm::degrees(atan2(front.z, front.x));
                    pitch = glm::degrees(asin(front.y));
                    UpdateVectors();
                    UpdateViewMatrix();
				}

                const glm::mat4& GetViewMatrix() const { return view; }
                const glm::mat4& GetProjectionMatrix() const { return projection; }
                const glm::vec3& GetPosition() const { return position; }

                glm::vec3 GetForward() {
                    return front;
                }

                glm::vec3 GetUp() {
                    return up;
                }

                glm::vec3 GetRight() {
                    return right;
                }

            private:
                void UpdateVectors()
                {
                    glm::vec3 front;
                    front.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch)); 
                    front.z = cos(glm::radians(pitch)) * sin(glm::radians(yaw));
                    front.y = sin(glm::radians(pitch));                          
                    front = glm::normalize(front);
                    this->front = front;
                    // Recalculate right and up vectors
                    right = glm::normalize(glm::cross(-front, worldUp));          
                    up = glm::normalize(glm::cross(right, front));
                }

                void UpdateViewMatrix()
                {
                    view = glm::lookAt(position, position + front, up);
                }

                void UpdateProjectionMatrix()
                {
                    projection = glm::perspective(fov, aspect, near, far);
                    // Vulkan: flip Y and adjust depth range
                    projection[1][1] *= -1.0f;
                }

            private:
                glm::vec3 position{0.0f, 0.0f, 5.0f};
                glm::vec3 front{0.0f, 0.0f, 1.0f};
                glm::vec3 up{0.0f, 1.0f, 0.0f};
                glm::vec3 right{1.0f, 0.0f, 0.0f};
                glm::vec3 worldUp{0.0f, 1.0f, 0.0f};

                float yaw = 90.0f;   // facing -Z
                float pitch = 0.0f;

                float fov;
                float aspect;
                float near;
                float far;

                glm::mat4 view{1.0f};
                glm::mat4 projection{1.0f};
            };

        }
    }
}