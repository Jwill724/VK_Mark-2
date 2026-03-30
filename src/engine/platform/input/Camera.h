#pragma once

#include "UserInput.h"
#include "engine/platform/profiler/Profiler.h"

constexpr float CAMERA_MIN_FOV = 70.0f;
constexpr float CAMERA_MAX_FOV = 103.0f;

class Camera {
public:
	Camera() = default;
	~Camera() = default;

	inline const glm::vec3& getPosition() const { return _position; }
	inline const glm::vec3& getVelocity() const { return _velocity; }
	inline const glm::vec3& getView() const { return _currentView; }

	inline float getPitch() const { return _pitch; }
	inline float getYaw() const { return _yaw; }

	inline float getAcceleration() const { return _acceleration; }
	inline float getDamping() const { return _damping; }

	inline float getFovY() const { return _fovY; }
	inline float getNearClip() const { return _nearClip; }
	inline float getFarClip() const { return _farClip; }

	inline float getSensitivity() const { return _sensitivity; }
	inline float getMaxSpeed() const { return _maxSpeed; }
	inline float getMinSpeed() const { return _minSpeed; }

	inline float getAperture() const { return _aperture; }
	inline float getShutterSpeed() const { return _shutterSpeed; }
	inline float getISO() const { return _iso; }

	inline glm::vec2 getDelta() const { return _delta; }

	// https://github.com/PanosK92/SpartanEngine/blob/1621c3e27fe671a029bbd0f9ccf22fd1c6c1fb3c/source/runtime/World/Components/Camera.h#L107
	inline float getExposure() const {
		// computed ev (using squared aperture for photometric accuracy)
		// note: this calculates the exposure scale factor (1/l_avg)
		float ev100 = std::log2((_aperture * _aperture) / _shutterSpeed * 100.0f / _iso);
		// standard standard output sensitivity (sos) calculation
		// 1.2 is a common calibration constant (matches ue5/frostbite)
		// this maps the average scene luminance to middle grey (0.18)
		const float calibration_constant = 1.2f;
		float base_exposure = 1.0f / (calibration_constant * std::pow(2.0f, ev100));
		return base_exposure;
	}

	inline void setPosition(const glm::vec3& pos) { _position = pos; }
	inline void setVelocity(const glm::vec3& vel) { _velocity = vel; }

	inline void setPitch(float pitch) { _pitch = pitch; }
	inline void setYaw(float yaw) { _yaw = yaw; }

	inline void setAcceleration(float accel) { _acceleration = accel; }
	inline void setDamping(float damping) { _damping = damping; }

	inline void setFovY(float fov) { _fovY = fov; }
	inline void setNearClip(float nearClip) { _nearClip = nearClip; }
	inline void setFarClip(float farClip) { _farClip = farClip; }

	inline void setSensitivity(float s) { _sensitivity = s; }
	inline void setMaxSpeed(float s) { _maxSpeed = s; }
	inline void setMinSpeed(float s) { _minSpeed = s; }

	inline void setAperture(float aperture) { _aperture = aperture; }
	inline void setShutterSpeed(float shutter) { _shutterSpeed = shutter; }
	inline void setISO(float iso) { _iso = iso; }

	inline void setDelta(glm::vec2 delta) { _delta = delta; }


	glm::mat4 getViewMatrix() const;
	glm::mat4 getRotationMatrix() const;

	void processInput(GLFWwindow* window, Profiler& profiler, bool& isTemporalInvalid);
	void reset();

private:
	glm::vec3 _position{0.0f};
	glm::vec3 _velocity{0.0f};

	float _pitch{0.0f}; // vertical
	float _yaw{0.0f};   // horizontal

	float _acceleration{0.0f};
	float _damping{0.0f};

	glm::vec3 _currentView{0.0f};

	glm::vec2 _delta{0.0f};

	float _sensitivity{0.0f};
	float _maxSpeed{0.0f};
	float _minSpeed{0.0f};

	float _fovY{0.0f};
	float _nearClip{0.1f};
	float _farClip{10000.0f}; // Good for d32 precision

	// Day time only defaults
	float _aperture{16.0f};
	float _shutterSpeed{1.0f / 60.0f};
	float _iso{100.0f};
};
