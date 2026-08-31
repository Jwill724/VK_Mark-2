#pragma once

#include "EngineTypes.h"

enum class ResizeReason : uint32_t
{
	None,
	WindowEvent,
	AcquireOutOfDate,
	PresentOutOfDate
};

enum class ResizePhase : uint32_t
{
	Idle,
	Requested,
	Draining,
	Applying
};

class ResizeCoordinator final
{
public:
	void Request(ResizeReason reason)
	{
		if (reason == ResizeReason::None) return;

		if (m_phase == ResizePhase::Idle)
		{
			m_phase = ResizePhase::Requested;
			m_reason = reason;
			m_coalesced = 1u;
			return;
		}

		++m_coalesced;

		if (reason != ResizeReason::WindowEvent)
			m_reason = reason;
	}

	bool IsPending() const noexcept { return m_phase != ResizePhase::Idle; }

	bool CanApply(Extents2D liveExtent) const noexcept
	{
		return m_phase == ResizePhase::Requested
			&& liveExtent.Width() > 0u
			&& liveExtent.Height() > 0u;
	}

	void EnterDrain()
	{
		ASSERT(m_phase == ResizePhase::Requested);
		m_phase = ResizePhase::Draining;
	}

	void EnterApply()
	{
		ASSERT(m_phase == ResizePhase::Draining);
		m_phase = ResizePhase::Applying;
	}

	void Complete(Extents2D appliedExtent)
	{
		ASSERT(m_phase == ResizePhase::Applying);
		m_applied = appliedExtent;
		m_reason = ResizeReason::None;
		++m_generation;

		if (m_coalesced > 1u)
		{
			m_phase = ResizePhase::Requested;
			m_reason = ResizeReason::WindowEvent;
			m_coalesced = 1u;
			return;
		}

		m_phase = ResizePhase::Idle;
		m_coalesced = 0u;
	}

	ResizePhase  GetPhase()      const noexcept { return m_phase; }
	ResizeReason GetReason()     const noexcept { return m_reason; }
	Extents2D    GetApplied()    const noexcept { return m_applied; }
	uint64_t     GetGeneration() const noexcept { return m_generation; }
	uint32_t     GetCoalesced()  const noexcept { return m_coalesced; }

	inline static const char* ToString(ResizePhase phase)
	{
		switch (phase)
		{
		case ResizePhase::Idle:      return "Idle";
		case ResizePhase::Requested: return "Requested";
		case ResizePhase::Draining:  return "Draining";
		case ResizePhase::Applying:  return "Applying";
		}
		return "?";
	}

	inline static const char* ToString(ResizeReason reason)
	{
		switch (reason)
		{
		case ResizeReason::None:             return "None";
		case ResizeReason::WindowEvent:      return "WindowEvent";
		case ResizeReason::AcquireOutOfDate: return "AcquireOutOfDate";
		case ResizeReason::PresentOutOfDate: return "PresentOutOfDate";
		}
		return "?";
	}

private:
	ResizePhase  m_phase = ResizePhase::Idle;
	ResizeReason m_reason = ResizeReason::None;
	Extents2D    m_applied{};
	uint64_t     m_generation = 0ull;
	uint32_t     m_coalesced = 0u;
};
