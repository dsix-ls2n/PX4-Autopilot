/****************************************************************************
 *
 *   Copyright (c) 2013-2022 PX4 Development Team. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name PX4 nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/**
 * @file VTOL_att_control_main.cpp
 * Implementation of an attitude controller for VTOL airframes. This module receives data
 * from both the fixed wing- and the multicopter attitude controllers and processes it.
 * It computes the correct actuator controls depending on which mode the vehicle is in (hover,forward-
 * flight or transition). It also publishes the resulting controls on the actuator controls topics.
 *
 * @author Roman Bapst 		<bapstr@ethz.ch>
 * @author Lorenz Meier 	<lm@inf.ethz.ch>
 * @author Thomas Gubler	<thomasgubler@gmail.com>
 * @author David Vorsin		<davidvorsin@gmail.com>
 * @author Sander Smeets	<sander@droneslab.com>
 * @author Andreas Antener 	<andreas@uaventure.com>
 *
 */

#pragma once

#include <drivers/drv_hrt.h>
#include <drivers/drv_pwm_output.h>
#include <lib/geo/geo.h>
#include <lib/mathlib/mathlib.h>
#include <lib/perf/perf_counter.h>
#include <matrix/math.hpp>
#include <px4_platform_common/px4_config.h>
#include <px4_platform_common/defines.h>
#include <px4_platform_common/module.h>
#include <px4_platform_common/module_params.h>
#include <px4_platform_common/posix.h>
#include <px4_platform_common/px4_work_queue/WorkItem.hpp>
#include <uORB/Publication.hpp>
#include <uORB/PublicationMulti.hpp>
#include <uORB/Subscription.hpp>
#include <uORB/SubscriptionCallback.hpp>
#include <uORB/topics/action_request.h>
#include <uORB/topics/actuator_controls.h>
#include <uORB/topics/airspeed_validated.h>
#include <uORB/topics/vehicle_air_data.h>
#include <uORB/topics/parameter_update.h>
#include <uORB/topics/position_setpoint_triplet.h>
#include <uORB/topics/tecs_status.h>
#include <uORB/topics/vehicle_attitude.h>
#include <uORB/topics/vehicle_attitude_setpoint.h>
#include <uORB/topics/vehicle_command.h>
#include <uORB/topics/vehicle_command_ack.h>
#include <uORB/topics/vehicle_control_mode.h>
#include <uORB/topics/vehicle_land_detected.h>
#include <uORB/topics/vehicle_local_position.h>
#include <uORB/topics/vehicle_local_position_setpoint.h>
#include <uORB/topics/vtol_vehicle_status.h>
#include <uORB/topics/vehicle_status.h>
#include <uORB/topics/vehicle_thrust_setpoint.h>
#include <uORB/topics/vehicle_torque_setpoint.h>
#include "standard.h"
#include "tailsitter.h"
#include "tiltrotor.h"

using namespace time_literals;

extern "C" __EXPORT int vtol_att_control_main(int argc, char *argv[]);

static constexpr float kMaxVTOLAttitudeControlTimeStep = 0.1f; // max time step [s]

class VtolAttitudeControl : public ModuleBase<VtolAttitudeControl>, public ModuleParams, public px4::WorkItem
{
public:

	enum class QuadchuteReason {
		TransitionTimeout = 0,
		ExternalCommand,
		MinimumAltBreached,
		LossOfAlt,
		LargeAltError,
		MaximumPitchExceeded,
		MaximumRollExceeded,
	};

	VtolAttitudeControl();
	~VtolAttitudeControl() override;

	/** @see ModuleBase */
	static int task_spawn(int argc, char *argv[]);

	/** @see ModuleBase */
	static int custom_command(int argc, char *argv[]);

	/** @see ModuleBase */
	static int print_usage(const char *reason = nullptr);

	bool init();

	bool is_fixed_wing_requested() { return _transition_command == vtol_vehicle_status_s::VEHICLE_VTOL_STATE_FW; };
	void quadchute(QuadchuteReason reason);
	int get_transition_command() {return _transition_command;}
	bool get_immediate_transition() {return _immediate_transition;}
	void reset_immediate_transition() {_immediate_transition = false;}

	float getAirDensity() const { return _air_density; }

	struct actuator_controls_s 			*get_actuators_fw_in() {return &_actuators_fw_in;}
	struct actuator_controls_s 			*get_actuators_mc_in() {return &_actuators_mc_in;}
	struct actuator_controls_s 			*get_actuators_out0() {return &_actuators_out_0;}
	struct actuator_controls_s 			*get_actuators_out1() {return &_actuators_out_1;}
	struct airspeed_validated_s 			*get_airspeed() {return &_airspeed_validated;}
	struct position_setpoint_triplet_s		*get_pos_sp_triplet() {return &_pos_sp_triplet;}
	struct tecs_status_s 				*get_tecs_status() {return &_tecs_status;}
	struct vehicle_attitude_s 			*get_att() {return &_v_att;}
	struct vehicle_attitude_setpoint_s		*get_att_sp() {return &_v_att_sp;}
	struct vehicle_attitude_setpoint_s 		*get_fw_virtual_att_sp() {return &_fw_virtual_att_sp;}
	struct vehicle_attitude_setpoint_s 		*get_mc_virtual_att_sp() {return &_mc_virtual_att_sp;}
	struct vehicle_control_mode_s 			*get_control_mode() {return &_v_control_mode;}
	struct vehicle_land_detected_s			*get_land_detected() {return &_land_detected;}
	struct vehicle_local_position_s 		*get_local_pos() {return &_local_pos;}
	struct vehicle_local_position_setpoint_s	*get_local_pos_sp() {return &_local_pos_sp;}
	struct vtol_vehicle_status_s			*get_vtol_vehicle_status() {return &_vtol_vehicle_status;}

	struct vehicle_torque_setpoint_s 		*get_torque_setpoint_0() {return &_torque_setpoint_0;}
	struct vehicle_torque_setpoint_s 		*get_torque_setpoint_1() {return &_torque_setpoint_1;}
	struct vehicle_thrust_setpoint_s 		*get_thrust_setpoint_0() {return &_thrust_setpoint_0;}
	struct vehicle_thrust_setpoint_s 		*get_thrust_setpoint_1() {return &_thrust_setpoint_1;}

private:
	void Run() override;

	uORB::SubscriptionCallbackWorkItem _actuator_inputs_fw{this, ORB_ID(actuator_controls_virtual_fw)};
	uORB::SubscriptionCallbackWorkItem _actuator_inputs_mc{this, ORB_ID(actuator_controls_virtual_mc)};

	uORB::SubscriptionInterval _parameter_update_sub{ORB_ID(parameter_update), 1_s};

	uORB::Subscription _action_request_sub{ORB_ID(action_request)};
	uORB::Subscription _airspeed_validated_sub{ORB_ID(airspeed_validated)};			// airspeed subscription
	uORB::Subscription _vehicle_air_data_sub{ORB_ID(vehicle_air_data)};
	uORB::Subscription _fw_virtual_att_sp_sub{ORB_ID(fw_virtual_attitude_setpoint)};
	uORB::Subscription _land_detected_sub{ORB_ID(vehicle_land_detected)};
	uORB::Subscription _local_pos_sp_sub{ORB_ID(vehicle_local_position_setpoint)};			// setpoint subscription
	uORB::Subscription _local_pos_sub{ORB_ID(vehicle_local_position)};			// sensor subscription
	uORB::Subscription _mc_virtual_att_sp_sub{ORB_ID(mc_virtual_attitude_setpoint)};
	uORB::Subscription _pos_sp_triplet_sub{ORB_ID(position_setpoint_triplet)};			// local position setpoint subscription
	uORB::Subscription _tecs_status_sub{ORB_ID(tecs_status)};
	uORB::Subscription _v_att_sub{ORB_ID(vehicle_attitude)};		//vehicle attitude subscription
	uORB::Subscription _v_control_mode_sub{ORB_ID(vehicle_control_mode)};	//vehicle control mode subscription
	uORB::Subscription _vehicle_cmd_sub{ORB_ID(vehicle_command)};
	uORB::Subscription _vehicle_status_sub{ORB_ID(vehicle_status)};

	uORB::Publication<actuator_controls_s>		_actuators_0_pub{ORB_ID(actuator_controls_0)};		//input for the mixer (roll,pitch,yaw,thrust)
	uORB::Publication<actuator_controls_s>		_actuators_1_pub{ORB_ID(actuator_controls_1)};
	uORB::Publication<vehicle_attitude_setpoint_s>	_v_att_sp_pub{ORB_ID(vehicle_attitude_setpoint)};
	uORB::Publication<vtol_vehicle_status_s>	_vtol_vehicle_status_pub{ORB_ID(vtol_vehicle_status)};
	uORB::PublicationMulti<vehicle_thrust_setpoint_s>	_vehicle_thrust_setpoint0_pub{ORB_ID(vehicle_thrust_setpoint)};
	uORB::PublicationMulti<vehicle_torque_setpoint_s>	_vehicle_torque_setpoint0_pub{ORB_ID(vehicle_torque_setpoint)};
	uORB::PublicationMulti<vehicle_thrust_setpoint_s>	_vehicle_thrust_setpoint1_pub{ORB_ID(vehicle_thrust_setpoint)};
	uORB::PublicationMulti<vehicle_torque_setpoint_s>	_vehicle_torque_setpoint1_pub{ORB_ID(vehicle_torque_setpoint)};

	orb_advert_t	_mavlink_log_pub{nullptr};	// mavlink log uORB handle

	vehicle_attitude_setpoint_s		_v_att_sp{};			//vehicle attitude setpoint
	vehicle_attitude_setpoint_s 		_fw_virtual_att_sp{};	// virtual fw attitude setpoint
	vehicle_attitude_setpoint_s 		_mc_virtual_att_sp{};	// virtual mc attitude setpoint

	actuator_controls_s			_actuators_fw_in{};	//actuator controls from fw_att_control
	actuator_controls_s			_actuators_mc_in{};	//actuator controls from mc_att_control
	actuator_controls_s			_actuators_out_0{};	//actuator controls going to the mc mixer
	actuator_controls_s			_actuators_out_1{};	//actuator controls going to the fw mixer (used for elevons)

	vehicle_torque_setpoint_s		_torque_setpoint_0{};
	vehicle_torque_setpoint_s		_torque_setpoint_1{};
	vehicle_thrust_setpoint_s		_thrust_setpoint_0{};
	vehicle_thrust_setpoint_s		_thrust_setpoint_1{};

	airspeed_validated_s 				_airspeed_validated{};			// airspeed
	position_setpoint_triplet_s		_pos_sp_triplet{};
	tecs_status_s				_tecs_status{};
	vehicle_attitude_s			_v_att{};				//vehicle attitude
	vehicle_control_mode_s			_v_control_mode{};	//vehicle control mode
	vehicle_land_detected_s			_land_detected{};
	vehicle_local_position_s		_local_pos{};
	vehicle_local_position_setpoint_s	_local_pos_sp{};
	vtol_vehicle_status_s 			_vtol_vehicle_status{};

	float _air_density{CONSTANTS_AIR_DENSITY_SEA_LEVEL_15C};	// [kg/m^3]

	hrt_abstime _last_run_timestamp{0};

	/* for multicopters it is usual to have a non-zero idle speed of the engines
	 * for fixed wings we want to have an idle speed of zero since we do not want
	 * to waste energy when gliding. */
	int		_transition_command{vtol_vehicle_status_s::VEHICLE_VTOL_STATE_MC};
	bool		_immediate_transition{false};

	VtolType	*_vtol_type{nullptr};	// base class for different vtol types

	bool		_initialized{false};

	perf_counter_t	_loop_perf;			/**< loop performance counter */

	void		action_request_poll();
	void		vehicle_cmd_poll();

	void 		parameters_update();

	DEFINE_PARAMETERS(
		(ParamInt<px4::params::VT_TYPE>) _param_vt_type
	)
};
