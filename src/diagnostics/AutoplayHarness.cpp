#include "diagnostics/AutoplayHarness.hpp"

#include "game/LevelCollision.hpp"
#include "game/PlayerPhysicsConstants.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

namespace usm::diagnostics {
namespace {

float distance2D(const assets::Vector3& left,
                 const assets::Vector3& right) noexcept {
    const float x = left.x - right.x;
    const float y = left.y - right.y;
    return std::sqrt(x * x + y * y);
}

void writeU16(std::ofstream& stream, std::uint16_t value) {
    const char bytes[2]{static_cast<char>(value & 0xffU),
                        static_cast<char>((value >> 8U) & 0xffU)};
    stream.write(bytes, sizeof(bytes));
}

void writeU32(std::ofstream& stream, std::uint32_t value) {
    const char bytes[4]{static_cast<char>(value & 0xffU),
                        static_cast<char>((value >> 8U) & 0xffU),
                        static_cast<char>((value >> 16U) & 0xffU),
                        static_cast<char>((value >> 24U) & 0xffU)};
    stream.write(bytes, sizeof(bytes));
}

std::string sanitizeLabel(std::string_view label) {
    std::string result;
    result.reserve(label.size());
    for (const char character : label) {
        const bool valid = (character >= 'a' && character <= 'z') ||
                           (character >= 'A' && character <= 'Z') ||
                           (character >= '0' && character <= '9') ||
                           character == '-' || character == '_';
        result.push_back(valid ? character : '-');
    }
    return result.empty() ? "frame" : result;
}

std::string visibleRoomList(std::span<const bool> rooms) {
    std::string result;
    for (std::size_t index = 0; index < rooms.size(); ++index) {
        if (!rooms[index]) {
            continue;
        }
        if (!result.empty()) {
            result += '|';
        }
        result += std::to_string(index + 1);
    }
    return result;
}

} // namespace

Result AutoplayHarness::initialize(const std::filesystem::path& scriptPath,
                                   const std::filesystem::path& outputPath) {
    *this = {};
    outputPath_ = outputPath;
    std::error_code error;
    std::filesystem::create_directories(outputPath_, error);
    if (error) {
        return Result::failure("Could not create autoplay output directory: " +
                               error.message());
    }
    Result result = parseScript(scriptPath);
    if (!result) {
        return result;
    }
    frameLog_.open(outputPath_ / "frames.csv", std::ios::trunc);
    roomLog_.open(outputPath_ / "rooms.csv", std::ios::trunc);
    enemyLog_.open(outputPath_ / "enemies.csv", std::ios::trunc);
    projectileLog_.open(outputPath_ / "enemy-projectiles.csv",
                        std::ios::trunc);
    objectLog_.open(outputPath_ / "objects.csv", std::ios::trunc);
    bonusLog_.open(outputPath_ / "bonuses.csv", std::ios::trunc);
    hostageLog_.open(outputPath_ / "hostages.csv", std::ios::trunc);
    dropLog_.open(outputPath_ / "drops.csv", std::ios::trunc);
    eventLog_.open(outputPath_ / "events.csv", std::ios::trunc);
    cinematicAssetLog_.open(outputPath_ / "cinematics.csv", std::ios::trunc);
    sceneNodeAssetLog_.open(outputPath_ / "scene-nodes.csv", std::ios::trunc);
    objectAssetLog_.open(outputPath_ / "object-assets.csv", std::ios::trunc);
    triggerAssetLog_.open(outputPath_ / "trigger-assets.csv", std::ios::trunc);
    waypointAssetLog_.open(outputPath_ / "waypoint-assets.csv",
                           std::ios::trunc);
    webGrabPointAssetLog_.open(outputPath_ / "web-grab-point-assets.csv",
                               std::ios::trunc);
    slideAssetLog_.open(outputPath_ / "slide-assets.csv", std::ios::trunc);
    checkPointAssetLog_.open(outputPath_ / "checkpoint-assets.csv",
                             std::ios::trunc);
    cameraAreaAssetLog_.open(outputPath_ / "camera-areas.csv",
                             std::ios::trunc);
    geometryAssetLog_.open(outputPath_ / "geometry-surfaces.csv",
                           std::ios::trunc);
    materialAssetLog_.open(outputPath_ / "materials.csv", std::ios::trunc);
    textureAssetLog_.open(outputPath_ / "textures.csv", std::ios::trunc);
    skyTriangleAssetLog_.open(outputPath_ / "sky-triangles.csv",
                              std::ios::trunc);
    colladaNodeAssetLog_.open(outputPath_ / "bdae-nodes.csv", std::ios::trunc);
    collisionAssetLog_.open(outputPath_ / "collision-surfaces.csv",
                            std::ios::trunc);
    collisionTriangleLog_.open(outputPath_ / "collision-triangles.csv",
                               std::ios::trunc);
    navigationTriangleLog_.open(outputPath_ / "navigation-triangles.csv",
                                std::ios::trunc);
    playerStateAssetLog_.open(outputPath_ / "player-states.csv",
                              std::ios::trunc);
    if (!frameLog_ || !roomLog_ || !enemyLog_ || !projectileLog_ ||
        !objectLog_ ||
        !bonusLog_ || !hostageLog_ || !dropLog_ || !eventLog_ ||
        !cinematicAssetLog_ || !sceneNodeAssetLog_ ||
        !objectAssetLog_ ||
        !triggerAssetLog_ ||
        !waypointAssetLog_ || !webGrabPointAssetLog_ || !slideAssetLog_ ||
        !checkPointAssetLog_ ||
        !cameraAreaAssetLog_ ||
        !geometryAssetLog_ || !materialAssetLog_ || !textureAssetLog_ ||
        !skyTriangleAssetLog_ ||
        !colladaNodeAssetLog_ ||
        !collisionAssetLog_ || !collisionTriangleLog_ ||
        !navigationTriangleLog_ ||
        !playerStateAssetLog_) {
        return Result::failure("Could not create autoplay trace files");
    }
    frameLog_ << "frame,real_ms,game_ms,slow_motion_denominator,phase,controls,attribution,player_x,player_y,"
                 "player_z,render_x,render_y,render_z,attack_root_x,"
                 "attack_root_y,attack_root_z,facing_x,facing_y,facing_z,"
                 "health,skill_points,combo_score,animation,"
                 "animation_ms,state_id,state_name,punch_transition_ready,"
                 "punch_attack_transition_ready,"
                 "jump_attack_transition_ready,jump_release_attack_transition_ready,"
                 "web_attack_transition_ready,web_held_attack_transition_ready,"
                 "on_wall,cinematic_motion,cinematic_motion_ms,"
                 "cinematic_motion_duration_ms,"
                  "camera_area,last_checkpoint,cinematic,active_cinematics,qte,hostage_qte,tutorial,"
                 "death_screen,death_alpha,death_confirmation,"
                 "death_confirmation_selection,exit_menu,exit_menu_state,"
                 "main_menu_requested,restore,restore_alpha,"
                 "visible_rooms,"
                 "input_right,input_forward,"
                 "camera_x,camera_y,camera_z,target_x,target_y,target_z,"
                 "rhino_qte_active,rhino_qte_state,rhino_qte_state_ms,"
                 "rhino_qte_button_ms,rhino_qte_duration_ms,"
                 "rhino_qte_completed_actions,"
                 "rhino_qte_required_actions,rhino_qte_progress,"
                 "boss_progress_visible,boss_progress_closing,"
                 "boss_progress_failed,boss_progress_boss,"
                 "boss_progress_distance,boss_progress_failure_distance,"
                 "boss_progress_ratio,"
                 "transport_state,transport_elapsed_ms,transport_scale,"
                 "level_ended,game_ended,cinematic_letterbox,wall_web_phase,"
                 "wall_web_target,wall_web_angle,wall_web_completed_actions,wall_web_line\n";
    roomLog_ << "frame,real_ms,object_id,room_id,moving,active,"
                "linked_waypoint,target_waypoint,x,y,z,vx,vy,vz\n";
    enemyLog_ << "frame,real_ms,object_id,type_id,x,y,z,health,visible,ai,"
                 "physics_active,detected,behavior,animation,animation_ms,animation_speed,"
                 "animation_loop,animation_reverse,collision_radius,"
                 "collision_height,vertical_velocity,hurt_state,hit_type,"
                 "hurt_vx,hurt_vy,hurt_vz,grounded,"
                 "anchored_without_support,cinematic_motion,"
                 "cinematic_motion_ms,cinematic_motion_duration_ms,"
                 "cinematic_action,cinematic_action_object,"
                 "melee_attack_active,melee_attack_registered,"
                 "melee_cooldown_ms,range_cooldown_ms,"
                 "sandman_task,sandman_jump_ms,sandman_jump_duration_ms,"
                 "rhino_task,rhino_task_ms,rhino_melee_remaining,"
                 "rhino_dash_x,rhino_dash_y,rhino_phase,"
                 "rhino_sequence_cycle,phantom_task,phantom_task_ms,"
                 "phantom_sequence_index,electro_task,electro_task_ms,"
                 "electro_phase,electro_sequence_index,"
                 "electro_range_remaining,electro_range_wait_ms,"
                 "electro_dash_remaining,electro_dash_target_x,"
                 "electro_dash_target_y,electro_dash_target_z,"
                 "on_wall,wall_attached,wall_normal_x,wall_normal_y,"
                 "wall_normal_z,wall_behavior_state,wall_target_x,"
                 "wall_target_y,wall_target_z,wall_web_captured\n";
    projectileLog_ <<
        "frame,real_ms,kind,source_object_id,room,x,y,z,vx,vy,vz,"
        "phase,phase_ms,damage,active,scale\n";
    objectLog_ << "frame,real_ms,object_id,kind,x,y,z,health,visible,"
                   "physics_enabled,collision_enabled,destruction_phase,"
                   "comic_collected,animation,animation_ms,animation_loop,"
                   "electric_state,electric_state_ms,electric_switch_active,"
                   "platform_motion_state,platform_motion_active,"
                   "platform_motion_ms,platform_target_waypoint,"
                   "train_active,train_cut,train_speed,"
                   "train_target_waypoint,train_previous,train_next,"
                   "train_dir_x,train_dir_y,train_dir_z,"
                   "train_rot_x,train_rot_y,train_rot_z,train_rot_w,"
                   "train_vx,train_vy,train_vz,"
                   "cinematic_motion,cinematic_motion_ms,"
                   "cinematic_motion_duration_ms,bridge_state,bridge_state_seconds,bridge_vz,"
                   "slide_car_state,slide_car_state_seconds,slide_car_bridge,"
                   "slide_car_vx,slide_car_vy,slide_car_vz,"
                   "slide_car_rot_x,slide_car_rot_y,slide_car_rot_z,slide_car_rot_w,"
                   "area_damage_state,area_damage_state_ms,"
                   "area_damage_wait_ms,area_damage_contact_cooldown_ms,"
                   "area_damage_player_hit\n";
    bonusLog_ << "frame,real_ms,object_id,type,room,x,y,z,visible,"
                 "orb_active,progress\n";
    hostageLog_ << "frame,real_ms,object_id,room,phase,phase_ms,prompt_visible,"
                    "qte_elapsed_ms,qte_duration_ms,completed_actions,"
                    "required_actions\n";
    dropLog_ << "frame,real_ms,object_id,owner_area,room,x,y,z,phase,"
                "visible,physics_enabled,hit_player,velocity,delay_ms\n";
    eventLog_ << "real_ms,frame,type,detail\n";
    cinematicAssetLog_
        << "cinematic_id,cinematic_name,script_file,thread_type,"
           "thread_object_id,thread_name,command_ms,command_id,command_name,"
           "attributes\n";
    sceneNodeAssetLog_
        << "source,room,node_id,parent_id,scene_type,name,game_type,visible,"
           "collision,mesh_file,animation_file,initial_animation,"
           "position_x,position_y,position_z,world_x,world_y,world_z,"
           "rotation_x,rotation_y,rotation_z,rotation_w,scale_x,scale_y,"
           "scale_z,user_attributes\n";
    objectAssetLog_
        << "object_id,name,game_type,kind_id,room,visible,has_collision,"
           "additive,initial_animation,initial_animation_loop,"
           "position_x,position_y,position_z,rotation_x,rotation_y,"
           "rotation_z,rotation_w,scale_x,scale_y,scale_z,"
           "m00,m01,m02,m03,m10,m11,m12,m13,m20,m21,m22,m23,"
           "m30,m31,m32,m33,electric_off_ms,electric_on_ms,"
           "electric_ready_ms,electric_delay_ms,electric_damage,"
           "electric_initial_active,electric_initial_state,platform_park_ms,"
           "platform_line_speed,platform_initial_active,"
           "platform_active_forever,platform_linked_waypoint,"
           "train_line_speed,train_initial_active,train_linked_waypoint,"
           "train_previous,train_next,train_life_duration_ms,"
           "train_can_transport,train_kills_player,"
           "area_damage_type,area_damage_begin_delay_ms,"
           "area_damage_random_low_ms,area_damage_random_high_ms,"
           "area_damage_ignore_physics,area_damage_active_forever,"
           "area_damage_automatic_detection,area_damage_damage,"
           "collision_min_x,collision_min_y,collision_min_z,collision_max_x,"
           "collision_max_y,collision_max_z,has_collision_bounds\n";
    triggerAssetLog_
        << "trigger_id,name,room,enabled,auto_disabled,oriented_box,"
           "out_to_in_cinematic,in_to_out_cinematic,while_inside_cinematic,"
           "while_outside_cinematic,position_x,position_y,position_z,"
           "rotation_x,rotation_y,rotation_z,rotation_w,scale_x,scale_y,"
           "scale_z,size_x,size_y,size_z,m00,m01,m02,m03,m10,m11,m12,m13,"
           "m20,m21,m22,m23,m30,m31,m32,m33\n";
    waypointAssetLog_
        << "waypoint_id,name,room,position_x,position_y,position_z,enabled,"
           "electric_shock,next_1,next_2,use_gravity_when_end,unstandable,"
           "jump_direction,time_to_me,linked_camera_area\n";
    webGrabPointAssetLog_
        << "web_grab_point_id,room,position_x,position_y,position_z,"
           "direction_control_point,direction_x,direction_y,direction_z,"
           "length,visible_length,vertical_angle_degrees,"
           "horizontal_angle_degrees,exit_speed,cannot_control,"
           "target_waypoint,target_waypoint_x,target_waypoint_y,"
           "target_waypoint_z,target_slide\n";
    slideAssetLog_
        << "slide_id,name,room,position_x,position_y,position_z,"
           "linked_waypoint,enabled,electric_shock,waypoints\n";
    checkPointAssetLog_
        << "checkpoint_id,room,position_x,position_y,position_z,"
           "rotation_x,rotation_y,rotation_z,rotation_w,"
           "scale_x,scale_y,scale_z,size_x,size_y,size_z,save_position,"
           "enabled,oriented_box,linked_waypoint,"
           "m00,m01,m02,m03,m10,m11,m12,m13,m20,m21,m22,m23,"
           "m30,m31,m32,m33\n";
    cameraAreaAssetLog_
        << "area_id,next_1,next_2,next_3,next_4,switch_1_units,"
           "switch_2_units,switch_3_units,switch_4_units,inverse_normal,"
           "height,z_follow_rate,disabled,far_plane_offset";
    for (std::size_t point = 0; point < 4; ++point) {
        const std::string suffix = std::to_string(point + 1);
        cameraAreaAssetLog_
            << ",point_" << suffix << "_id,point_" << suffix
            << "_x,point_" << suffix << "_y,point_" << suffix
            << "_z,point_" << suffix << "_direction_x,point_" << suffix
            << "_direction_y,point_" << suffix
            << "_direction_z,point_" << suffix << "_distance,point_"
            << suffix << "_target_x,point_" << suffix
            << "_target_y,point_" << suffix << "_target_z,point_"
            << suffix << "_target_height_offset";
    }
    cameraAreaAssetLog_
        << ",must_invisible_rooms,must_visible_rooms\n";
    geometryAssetLog_
        << "room,geometry,buffer,material,vertices,indices,uses_uv2,"
           "lightmap_image,min_x,min_y,min_z,max_x,max_y,max_z\n";
    materialAssetLog_
        << "owner_kind,owner_id,owner_name,room,owner_additive,mesh_source,geometry,buffer,"
           "material,material_id,effect,primitive,vertices,indices,"
           "diffuse_index,diffuse_image,diffuse_path,diffuse_has_alpha,"
           "secondary_index,secondary_image,secondary_path,"
           "secondary_has_alpha,lightmap_index,lightmap_image,lightmap_path,"
           "lightmap_has_alpha,secondary_mode,material_additive,back_face_culling,"
           "front_face_culling,transparent_alpha_channel,material_type_parameter,"
           "uses_uv2,vertex_alpha_min,vertex_alpha_max,min_x,min_y,min_z,"
           "max_x,max_y,max_z\n";
    textureAssetLog_
        << "owner_kind,owner_id,owner_name,mesh_source,image_index,image_name,"
           "image_path,width,height,contains_alpha,red_min,red_max,red_mean,"
           "green_min,green_max,green_mean,blue_min,blue_max,blue_mean,"
           "alpha_min,alpha_max,alpha_mean,alpha_zero_pixels,"
           "alpha_partial_pixels,alpha_opaque_pixels,black_pixels,"
           "opaque_black_pixels\n";
    skyTriangleAssetLog_
        << "geometry,buffer,material,triangle,first_x,first_y,first_z,"
           "first_u,first_v,second_x,second_y,second_z,second_u,second_v,"
           "third_x,third_y,third_z,third_u,third_v\n";
    colladaNodeAssetLog_
        << "owner_kind,owner_id,owner_name,mesh_source,node_index,parent_index,"
           "node_id,node_name,scope_id,local_x,local_y,local_z,quaternion_x,"
           "quaternion_y,quaternion_z,quaternion_w,scale_x,scale_y,scale_z,"
           "world_x,world_y,world_z,m00,m01,m02,m10,m11,m12,m20,m21,m22,"
           "geometry_indices\n";
    collisionAssetLog_
        << "room,geometry,surface_class,min_x,min_y,min_z,max_x,max_y,max_z\n";
    collisionTriangleLog_
        << "room,geometry,triangle,physics_flags,first_x,first_y,first_z,"
           "second_x,second_y,second_z,third_x,third_y,third_z,"
           "normal_x,normal_y,normal_z\n";
    navigationTriangleLog_
        << "room,geometry,triangle,first_x,first_y,first_z,"
           "second_x,second_y,second_z,third_x,third_y,third_z\n";
    playerStateAssetLog_
        << "state_id,state_name,state_class,motion_type,motion_0,motion_1,"
           "motion_2,motion_3,primary_animation,animation_ids,"
           "sound_trigger_frame,next_state_id,timing_0,timing_1,"
           "aux_0,aux_1,aux_2,aux_3,impact_frames,auxiliary_ids,"
           "transition_buttons,transition_predicates,transition_targets\n";
    recordEvent(0, "harness_start",
                "script=" + scriptPath.generic_string());
    return Result::success();
}

Result AutoplayHarness::parseScript(
    const std::filesystem::path& scriptPath) {
    std::ifstream stream(scriptPath);
    if (!stream) {
        return Result::failure("Could not open autoplay script: " +
                               scriptPath.string());
    }
    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(stream, line)) {
        ++lineNumber;
        const std::size_t comment = line.find('#');
        if (comment != std::string::npos) {
            line.erase(comment);
        }
        std::istringstream tokens(line);
        std::string command;
        if (!(tokens >> command)) {
            continue;
        }
        const auto invalid = [lineNumber](std::string_view reason) {
            return Result::failure("Autoplay script line " +
                                   std::to_string(lineNumber) + ": " +
                                   std::string(reason));
        };
        if (command == "fixed_step_ms") {
            if (!(tokens >> fixedStepMilliseconds_) ||
                fixedStepMilliseconds_ == 0 ||
                fixedStepMilliseconds_ > 100) {
                return invalid("fixed_step_ms must be in [1, 100]");
            }
            continue;
        }
        if (command == "sample_interval_ms") {
            if (!(tokens >> sampleIntervalMilliseconds_) ||
                sampleIntervalMilliseconds_ == 0) {
                return invalid("sample_interval_ms must be positive");
            }
            continue;
        }
        if (command == "capture_interval_ms") {
            if (!(tokens >> captureIntervalMilliseconds_)) {
                return invalid("capture_interval_ms requires a value");
            }
            continue;
        }
        if (command == "max_time_ms") {
            if (!(tokens >> maximumTimeMilliseconds_) ||
                maximumTimeMilliseconds_ == 0) {
                return invalid("max_time_ms must be positive");
            }
            continue;
        }
        if (command == "start_time_ms") {
            if (!(tokens >> startTimeMilliseconds_)) {
                return invalid("start_time_ms requires a value");
            }
            continue;
        }
        if (command == "render_size") {
            if (!(tokens >> renderWidth_ >> renderHeight_) ||
                renderWidth_ == 0 || renderHeight_ == 0) {
                return invalid("render_size requires positive width/height");
            }
            continue;
        }
        if (command == "auto_qte") {
            std::int32_t enabled = -1;
            if (!(tokens >> enabled) || (enabled != 0 && enabled != 1)) {
                return invalid("auto_qte requires zero or one");
            }
            autoQuickTimeActions_ = enabled != 0;
            continue;
        }

        Step step;
        step.sourceLine = lineNumber;
        if (command == "wait_gameplay") {
            step.kind = StepKind::WaitGameplay;
            if (!(tokens >> step.durationOrTimeoutMilliseconds)) {
                return invalid("wait_gameplay requires a timeout");
            }
        } else if (command == "wait_intro") {
            step.kind = StepKind::WaitIntro;
            if (!(tokens >> step.durationOrTimeoutMilliseconds) ||
                step.durationOrTimeoutMilliseconds == 0) {
                return invalid("wait_intro requires a positive timeout");
            }
        } else if (command == "wait_controls") {
            step.kind = StepKind::WaitControls;
            if (!(tokens >> step.durationOrTimeoutMilliseconds) ||
                step.durationOrTimeoutMilliseconds == 0) {
                return invalid("wait_controls requires a positive timeout");
            }
        } else if (command == "wait") {
            step.kind = StepKind::Wait;
            if (!(tokens >> step.durationOrTimeoutMilliseconds)) {
                return invalid("wait requires a duration");
            }
        } else if (command == "move_to" || command == "move_to_3d" ||
                   command == "climb_to") {
            step.kind = command == "move_to" ? StepKind::MoveTo
                : command == "climb_to" ? StepKind::ClimbTo : StepKind::MoveTo3D;
            if (!(tokens >> step.position.x >> step.position.y >>
                  step.position.z >> step.radius >>
                  step.durationOrTimeoutMilliseconds) ||
                step.radius <= 0.0F || !std::isfinite(step.radius) ||
                !std::isfinite(step.position.x) ||
                !std::isfinite(step.position.y) ||
                !std::isfinite(step.position.z) ||
                step.durationOrTimeoutMilliseconds == 0) {
                return invalid(command + " requires finite x y z, positive radius and timeout");
            }
        } else if (command == "move_input") {
            step.kind = StepKind::MoveInput;
            if (!(tokens >> step.durationOrTimeoutMilliseconds >>
                  step.position.x >> step.position.y) ||
                step.durationOrTimeoutMilliseconds == 0 ||
                !std::isfinite(step.position.x) ||
                !std::isfinite(step.position.y) ||
                std::abs(step.position.x) > 1.0F ||
                std::abs(step.position.y) > 1.0F) {
                return invalid(
                    "move_input requires duration right forward in [-1, 1]");
            }
        } else if (command == "move_until_wall") {
            step.kind = StepKind::MoveUntilWall;
            if (!(tokens >> step.position.x >> step.position.y >>
                  step.position.z >> step.durationOrTimeoutMilliseconds) ||
                step.durationOrTimeoutMilliseconds == 0) {
                return invalid("move_until_wall requires x y z timeout");
            }
        } else if (command == "move_until_state") {
            step.kind = StepKind::MoveUntilState;
            std::int32_t stateId = -1;
            if (!(tokens >> step.durationOrTimeoutMilliseconds >>
                  step.position.x >> step.position.y >> stateId) ||
                step.durationOrTimeoutMilliseconds == 0 || stateId < 0 ||
                stateId > std::numeric_limits<std::uint16_t>::max() ||
                !std::isfinite(step.position.x) ||
                !std::isfinite(step.position.y) ||
                std::abs(step.position.x) > 1.0F ||
                std::abs(step.position.y) > 1.0F) {
                return invalid(
                    "move_until_state requires timeout right forward "
                    "state_id");
            }
            step.objectIds.push_back(stateId);
        } else if (command == "move_to_until_state") {
            step.kind = StepKind::MoveToUntilState;
            std::int32_t stateId = -1;
            if (!(tokens >> step.position.x >> step.position.y >>
                  step.position.z >> stateId >>
                  step.durationOrTimeoutMilliseconds) ||
                step.durationOrTimeoutMilliseconds == 0 || stateId < 0 ||
                stateId > std::numeric_limits<std::uint16_t>::max() ||
                !std::isfinite(step.position.x) ||
                !std::isfinite(step.position.y) ||
                !std::isfinite(step.position.z)) {
                return invalid(
                    "move_to_until_state requires finite x y z, state_id "
                    "and positive timeout");
            }
            step.objectIds.push_back(stateId);
        } else if (command == "move_until_cinematic") {
            step.kind = StepKind::MoveUntilCinematic;
            std::int32_t cinematicId = -1;
            if (!(tokens >> step.position.x >> step.position.y >>
                  step.position.z >> cinematicId >>
                  step.durationOrTimeoutMilliseconds) ||
                cinematicId < 0) {
                return invalid(
                    "move_until_cinematic requires x y z cinematic_id timeout");
            }
            step.objectIds.push_back(cinematicId);
        } else if (command == "move_input_until_cinematic") {
            step.kind = StepKind::MoveInputUntilCinematic;
            std::int32_t cinematicId = -1;
            if (!(tokens >> step.durationOrTimeoutMilliseconds >>
                  step.position.x >> step.position.y >> cinematicId) ||
                step.durationOrTimeoutMilliseconds == 0 || cinematicId < 0 ||
                !std::isfinite(step.position.x) ||
                !std::isfinite(step.position.y) ||
                std::abs(step.position.x) > 1.0F ||
                std::abs(step.position.y) > 1.0F) {
                return invalid(
                    "move_input_until_cinematic requires timeout right "
                    "forward cinematic_id");
            }
            step.objectIds.push_back(cinematicId);
        } else if (command == "wait_cinematic_started") {
            step.kind = StepKind::WaitCinematicStarted;
            std::int32_t cinematicId = -1;
            if (!(tokens >> step.durationOrTimeoutMilliseconds >>
                  cinematicId) ||
                step.durationOrTimeoutMilliseconds == 0 || cinematicId < 0) {
                return invalid(
                    "wait_cinematic_started requires timeout cinematic_id");
            }
            step.objectIds.push_back(cinematicId);
        } else if (command == "wait_transport_state") {
            step.kind = StepKind::WaitTransportState;
            std::int32_t state = -2;
            if (!(tokens >> step.durationOrTimeoutMilliseconds >> state) ||
                step.durationOrTimeoutMilliseconds == 0 || state < -1 ||
                state > 2) {
                return invalid(
                    "wait_transport_state requires timeout state (-1..2)");
            }
            step.objectIds.push_back(state);
        } else if (command == "wait_death_screen") {
            step.kind = StepKind::WaitDeathScreen;
            if (!(tokens >> step.durationOrTimeoutMilliseconds) ||
                step.durationOrTimeoutMilliseconds == 0) {
                return invalid("wait_death_screen requires a timeout");
            }
        } else if (command == "wait_death_confirmation") {
            step.kind = StepKind::WaitDeathConfirmation;
            if (!(tokens >> step.durationOrTimeoutMilliseconds) ||
                step.durationOrTimeoutMilliseconds == 0) {
                return invalid(
                    "wait_death_confirmation requires a timeout");
            }
        } else if (command == "wait_exit_menu") {
            step.kind = StepKind::WaitExitMenu;
            if (!(tokens >> step.durationOrTimeoutMilliseconds) ||
                step.durationOrTimeoutMilliseconds == 0) {
                return invalid("wait_exit_menu requires a timeout");
            }
        } else if (command == "wait_main_menu_requested") {
            step.kind = StepKind::WaitMainMenuRequested;
            if (!(tokens >> step.durationOrTimeoutMilliseconds) ||
                step.durationOrTimeoutMilliseconds == 0) {
                return invalid(
                    "wait_main_menu_requested requires a timeout");
            }
        } else if (command == "cross_trigger") {
            step.kind = StepKind::CrossTrigger;
            std::int32_t triggerId = -1;
            std::int32_t cinematicId = -1;
            if (!(tokens >> triggerId >> cinematicId >>
                  step.durationOrTimeoutMilliseconds) ||
                triggerId < 0 || cinematicId < 0 ||
                step.durationOrTimeoutMilliseconds == 0) {
                return invalid(
                    "cross_trigger requires trigger_id cinematic_id timeout");
            }
            step.objectIds = {triggerId, cinematicId};
        } else if (command == "wait_enemies_grounded") {
            step.kind = StepKind::WaitEnemiesGrounded;
            if (!(tokens >> step.durationOrTimeoutMilliseconds)) {
                return invalid(
                    "wait_enemies_grounded requires timeout and enemy IDs");
            }
            std::int32_t objectId = -1;
            while (tokens >> objectId) {
                step.objectIds.push_back(objectId);
            }
            if (step.objectIds.empty()) {
                return invalid(
                    "wait_enemies_grounded requires at least one enemy ID");
            }
        } else if (command == "wait_enemies_active") {
            step.kind = StepKind::WaitEnemiesActive;
            if (!(tokens >> step.durationOrTimeoutMilliseconds)) {
                return invalid(
                    "wait_enemies_active requires timeout and enemy IDs");
            }
            std::int32_t objectId = -1;
            while (tokens >> objectId) {
                step.objectIds.push_back(objectId);
            }
            if (step.objectIds.empty()) {
                return invalid(
                    "wait_enemies_active requires at least one enemy ID");
            }
        } else if (command == "wait_enemy_melee_attack") {
            step.kind = StepKind::WaitEnemyMeleeAttack;
            std::int32_t objectId = -1;
            if (!(tokens >> step.durationOrTimeoutMilliseconds >> objectId) ||
                step.durationOrTimeoutMilliseconds == 0 || objectId < 0) {
                return invalid(
                    "wait_enemy_melee_attack requires timeout enemy_id");
            }
            step.objectIds.push_back(objectId);
        } else if (command == "wait_enemy_projectile") {
            step.kind = StepKind::WaitEnemyProjectile;
            std::int32_t objectId = -1;
            if (!(tokens >> step.durationOrTimeoutMilliseconds >> objectId) ||
                step.durationOrTimeoutMilliseconds == 0 || objectId < 0) {
                return invalid(
                    "wait_enemy_projectile requires timeout enemy_id");
            }
            step.objectIds.push_back(objectId);
        } else if (command == "set_enemy_ai") {
            step.kind = StepKind::SetEnemyAi;
            std::int32_t objectId = -1;
            std::int32_t enabled = -1;
            std::int32_t forcePlayerDetected = 1;
            if (!(tokens >> objectId >> enabled) || objectId < 0 ||
                (enabled != 0 && enabled != 1)) {
                return invalid(
                    "set_enemy_ai requires enemy_id enabled [force_detect]");
            }
            if (tokens >> forcePlayerDetected) {
                if (forcePlayerDetected != 0 && forcePlayerDetected != 1) {
                    return invalid(
                        "set_enemy_ai force_detect must be zero or one");
                }
            }
            step.objectIds.push_back(objectId);
            step.value = static_cast<float>(enabled);
            step.radius = static_cast<float>(forcePlayerDetected);
        } else if (command == "set_enemy_physics") {
            step.kind = StepKind::SetEnemyPhysics;
            std::int32_t objectId = -1;
            std::int32_t enabled = -1;
            if (!(tokens >> objectId >> enabled) || objectId < 0 ||
                (enabled != 0 && enabled != 1)) {
                return invalid(
                    "set_enemy_physics requires enemy_id enabled");
            }
            step.objectIds.push_back(objectId);
            step.value = static_cast<float>(enabled);
        } else if (command == "damage_enemy") {
            step.kind = StepKind::DamageEnemy;
            std::int32_t objectId = -1;
            if (!(tokens >> objectId >> step.value) || objectId < 0 ||
                step.value <= 0.0F || !std::isfinite(step.value)) {
                return invalid(
                    "damage_enemy requires enemy_id positive_damage");
            }
            step.objectIds.push_back(objectId);
        } else if (command == "wait_enemy_animation") {
            step.kind = StepKind::WaitEnemyAnimation;
            std::int32_t objectId = -1;
            if (!(tokens >> step.durationOrTimeoutMilliseconds >> objectId >>
                  step.label) ||
                step.durationOrTimeoutMilliseconds == 0 || objectId < 0) {
                return invalid(
                    "wait_enemy_animation requires timeout enemy_id animation");
            }
            step.objectIds.push_back(objectId);
        } else if (command == "wait_object_electric_state") {
            step.kind = StepKind::WaitObjectElectricState;
            std::int32_t objectId = -1;
            if (!(tokens >> step.durationOrTimeoutMilliseconds >> objectId >>
                  step.label) ||
                step.durationOrTimeoutMilliseconds == 0 || objectId < 0 ||
                (step.label != "release" && step.label != "off" &&
                 step.label != "warning")) {
                return invalid(
                    "wait_object_electric_state requires timeout object_id "
                    "release|off|warning");
            }
            step.objectIds.push_back(objectId);
        } else if (command == "wait_area_damage_state") {
            step.kind = StepKind::WaitAreaDamageState;
            std::int32_t objectId = -1;
            if (!(tokens >> step.durationOrTimeoutMilliseconds >> objectId >>
                  step.label) ||
                step.durationOrTimeoutMilliseconds == 0 || objectId < 0 ||
                (step.label != "delay" && step.label != "active" &&
                 step.label != "waiting")) {
                return invalid(
                    "wait_area_damage_state requires timeout object_id "
                    "delay|active|waiting");
            }
            step.objectIds.push_back(objectId);
        } else if (command == "wait_object_near") {
            step.kind = StepKind::WaitObjectNear;
            std::int32_t objectId = -1;
            if (!(tokens >> step.durationOrTimeoutMilliseconds >> objectId >>
                  step.position.x >> step.position.y >> step.position.z >>
                  step.radius) ||
                step.durationOrTimeoutMilliseconds == 0 || objectId < 0 ||
                step.radius <= 0.0F) {
                return invalid(
                    "wait_object_near requires timeout object_id x y z "
                    "radius");
            }
            step.objectIds.push_back(objectId);
        } else if (command == "wait_level_end") {
            step.kind = StepKind::WaitLevelEnd;
            if (!(tokens >> step.durationOrTimeoutMilliseconds) ||
                step.durationOrTimeoutMilliseconds == 0) {
                return invalid("wait_level_end requires a timeout");
            }
        } else if (command == "attack") {
            step.kind = StepKind::Attack;
            if (!(tokens >> step.durationOrTimeoutMilliseconds >>
                  step.radius) ||
                step.radius <= 0.0F) {
                return invalid("attack requires timeout preferred_distance IDs");
            }
            std::int32_t objectId = -1;
            while (tokens >> objectId) {
                step.objectIds.push_back(objectId);
            }
            if (step.objectIds.empty()) {
                return invalid("attack requires at least one enemy ID");
            }
        } else if (command == "attack_object") {
            step.kind = StepKind::AttackObject;
            std::int32_t objectId = -1;
            if (!(tokens >> step.durationOrTimeoutMilliseconds >>
                  step.radius >> objectId) ||
                step.durationOrTimeoutMilliseconds == 0 ||
                step.radius <= 0.0F || objectId < 0) {
                return invalid(
                    "attack_object requires timeout preferred_distance "
                    "object_id");
            }
            step.objectIds.push_back(objectId);
        } else if (command == "collect_bonus") {
            step.kind = StepKind::CollectBonus;
            std::int32_t objectId = -1;
            if (!(tokens >> step.durationOrTimeoutMilliseconds >> objectId) ||
                step.durationOrTimeoutMilliseconds == 0 || objectId < 0) {
                return invalid("collect_bonus requires timeout object_id");
            }
            step.objectIds.push_back(objectId);
        } else if (command == "rescue_hostage") {
            step.kind = StepKind::RescueHostage;
            std::int32_t objectId = -1;
            if (!(tokens >> step.durationOrTimeoutMilliseconds >> objectId) ||
                step.durationOrTimeoutMilliseconds == 0 || objectId < 0) {
                return invalid(
                    "rescue_hostage requires timeout object_id [qte|freed]");
            }
            if (!(tokens >> step.label)) {
                step.label = "freed";
            }
            if (step.label != "qte" && step.label != "freed") {
                return invalid(
                    "rescue_hostage target phase must be qte or freed");
            }
            step.objectIds.push_back(objectId);
        } else if (command == "wait_drop_hit") {
            step.kind = StepKind::WaitDropHit;
            std::int32_t objectId = -1;
            if (!(tokens >> step.durationOrTimeoutMilliseconds >> objectId) ||
                step.durationOrTimeoutMilliseconds == 0 || objectId < 0) {
                return invalid("wait_drop_hit requires timeout object_id");
            }
            step.objectIds.push_back(objectId);
        } else if (command == "collect_comic") {
            step.kind = StepKind::CollectComic;
            std::int32_t objectId = -1;
            if (!(tokens >> step.durationOrTimeoutMilliseconds >> objectId) ||
                step.durationOrTimeoutMilliseconds == 0 || objectId < 0) {
                return invalid("collect_comic requires timeout object_id");
            }
            step.objectIds.push_back(objectId);
        } else if (command == "jump") {
            step.kind = StepKind::Jump;
            if (tokens >> step.position.x) {
                if (!(tokens >> step.position.y) ||
                    !std::isfinite(step.position.x) ||
                    !std::isfinite(step.position.y)) {
                    return invalid("jump accepts optional right forward input");
                }
            }
        } else if (command == "punch") {
            step.kind = StepKind::Punch;
        } else if (command == "press_buttons") {
            step.kind = StepKind::PressButtons;
            std::string button;
            while (tokens >> button) {
                if (button == "jump") {
                    step.objectIds.push_back(8);
                } else if (button == "punch") {
                    step.objectIds.push_back(6);
                } else if (button == "web") {
                    step.objectIds.push_back(5);
                } else {
                    return invalid(
                        "press_buttons accepts jump, punch, and web");
                }
            }
            std::ranges::sort(step.objectIds);
            step.objectIds.erase(
                std::unique(step.objectIds.begin(), step.objectIds.end()),
                step.objectIds.end());
            if (step.objectIds.size() < 2) {
                return invalid(
                    "press_buttons requires at least two distinct buttons");
            }
        } else if (command == "punch_when_ready") {
            step.kind = StepKind::PunchWhenReady;
            if (!(tokens >> step.durationOrTimeoutMilliseconds) ||
                step.durationOrTimeoutMilliseconds == 0) {
                return invalid("punch_when_ready requires timeout");
            }
        } else if (command == "punch_attack_when_ready") {
            step.kind = StepKind::PunchAttackWhenReady;
            if (!(tokens >> step.durationOrTimeoutMilliseconds) ||
                step.durationOrTimeoutMilliseconds == 0) {
                return invalid("punch_attack_when_ready requires timeout");
            }
        } else if (command == "jump_attack_when_ready") {
            step.kind = StepKind::JumpAttackWhenReady;
            if (!(tokens >> step.durationOrTimeoutMilliseconds) ||
                step.durationOrTimeoutMilliseconds == 0) {
                return invalid("jump_attack_when_ready requires timeout");
            }
        } else if (command == "jump_release_attack_when_ready") {
            step.kind = StepKind::JumpReleaseAttackWhenReady;
            if (!(tokens >> step.durationOrTimeoutMilliseconds) ||
                step.durationOrTimeoutMilliseconds == 0) {
                return invalid(
                    "jump_release_attack_when_ready requires timeout");
            }
        } else if (command == "web_attack_when_ready") {
            step.kind = StepKind::WebAttackWhenReady;
            if (!(tokens >> step.durationOrTimeoutMilliseconds) ||
                step.durationOrTimeoutMilliseconds == 0) {
                return invalid("web_attack_when_ready requires timeout");
            }
        } else if (command == "web_held_attack_when_ready") {
            step.kind = StepKind::WebHeldAttackWhenReady;
            if (!(tokens >> step.durationOrTimeoutMilliseconds) ||
                step.durationOrTimeoutMilliseconds == 0) {
                return invalid("web_held_attack_when_ready requires timeout");
            }
        } else if (command == "spider_sense") {
            step.kind = StepKind::SpiderSense;
        } else if (command == "super_attack") {
            step.kind = StepKind::SuperAttack;
        } else if (command == "web_on") {
            step.kind = StepKind::WebOn;
        } else if (command == "web_off") {
            step.kind = StepKind::WebOff;
        } else if (command == "set_auto_qte") {
            step.kind = StepKind::SetAutoQuickTime;
            if (!(tokens >> step.value) || (step.value != 0.0F && step.value != 1.0F)) {
                return invalid("set_auto_qte requires zero or one");
            }
        } else if (command == "qte_tap") {
            step.kind = StepKind::QuickTimeTap;
        } else if (command == "menu_up") {
            step.kind = StepKind::MenuUp;
        } else if (command == "menu_down") {
            step.kind = StepKind::MenuDown;
        } else if (command == "menu_select") {
            step.kind = StepKind::MenuSelect;
        } else if (command == "teleport") {
            step.kind = StepKind::Teleport;
            if (!(tokens >> step.position.x >> step.position.y >>
                  step.position.z >> step.facing.x >> step.facing.y >>
                  step.facing.z)) {
                return invalid("teleport requires x y z facing_x facing_y facing_z");
            }
        } else if (command == "start_cinematic") {
            step.kind = StepKind::StartCinematic;
            std::int32_t cinematicId = -1;
            if (!(tokens >> cinematicId) || cinematicId < 0) {
                return invalid("start_cinematic requires cinematic_id");
            }
            step.objectIds.push_back(cinematicId);
        } else if (command == "capture") {
            step.kind = StepKind::Capture;
            if (!(tokens >> step.label)) {
                return invalid("capture requires a label");
            }
        } else if (command == "assert_near") {
            step.kind = StepKind::AssertNear;
            if (!(tokens >> step.position.x >> step.position.y >>
                  step.position.z >> step.radius) ||
                step.radius <= 0.0F) {
                return invalid("assert_near requires x y z radius");
            }
        } else if (command == "assert_camera_area") {
            step.kind = StepKind::AssertCameraArea;
            std::int32_t objectId = -1;
            if (!(tokens >> objectId) || objectId < 0) {
                return invalid("assert_camera_area requires object_id");
            }
            step.objectIds.push_back(objectId);
        } else if (command == "assert_last_checkpoint") {
            step.kind = StepKind::AssertLastCheckPoint;
            std::int32_t objectId = -1;
            if (!(tokens >> objectId) || objectId < -1) {
                return invalid(
                    "assert_last_checkpoint requires object_id or -1");
            }
            step.objectIds.push_back(objectId);
        } else if (command == "assert_enemy_near") {
            step.kind = StepKind::AssertEnemyNear;
            std::int32_t objectId = -1;
            if (!(tokens >> objectId >> step.position.x >> step.position.y >>
                  step.position.z >> step.radius) ||
                objectId < 0 || step.radius <= 0.0F) {
                return invalid(
                    "assert_enemy_near requires enemy_id x y z radius");
            }
            step.objectIds.push_back(objectId);
        } else if (command == "assert_enemy_health_below") {
            step.kind = StepKind::AssertEnemyHealthBelow;
            std::int32_t objectId = -1;
            if (!(tokens >> objectId >> step.value) || objectId < 0 ||
                !std::isfinite(step.value)) {
                return invalid(
                    "assert_enemy_health_below requires enemy_id value");
            }
            step.objectIds.push_back(objectId);
        } else if (command == "assert_enemy_health_near") {
            step.kind = StepKind::AssertEnemyHealthNear;
            std::int32_t objectId = -1;
            if (!(tokens >> objectId >> step.value >> step.radius) ||
                objectId < 0 || !std::isfinite(step.value) ||
                !std::isfinite(step.radius) || step.radius < 0.0F) {
                return invalid(
                    "assert_enemy_health_near requires enemy_id value "
                    "tolerance");
            }
            step.objectIds.push_back(objectId);
        } else if (command == "assert_enemy_melee_attack_active") {
            step.kind = StepKind::AssertEnemyMeleeAttackActive;
            std::int32_t objectId = -1;
            std::int32_t expected = -1;
            if (!(tokens >> objectId >> expected) || objectId < 0 ||
                (expected != 0 && expected != 1)) {
                return invalid(
                    "assert_enemy_melee_attack_active requires enemy_id 0|1");
            }
            step.objectIds = {objectId, expected};
        } else if (command == "assert_enemy_behavior") {
            step.kind = StepKind::AssertEnemyBehavior;
            std::int32_t objectId = -1;
            if (!(tokens >> objectId >> step.label) || objectId < 0) {
                return invalid(
                    "assert_enemy_behavior requires enemy_id behavior");
            }
            step.objectIds.push_back(objectId);
        } else if (command == "assert_enemy_distance_above") {
            step.kind = StepKind::AssertEnemyDistanceAbove;
            std::int32_t firstObjectId = -1;
            std::int32_t secondObjectId = -1;
            if (!(tokens >> firstObjectId >> secondObjectId >> step.value) ||
                firstObjectId < 0 || secondObjectId < 0 ||
                firstObjectId == secondObjectId || step.value <= 0.0F ||
                !std::isfinite(step.value)) {
                return invalid(
                    "assert_enemy_distance_above requires first_enemy_id "
                    "second_enemy_id distance");
            }
            step.objectIds.push_back(firstObjectId);
            step.objectIds.push_back(secondObjectId);
        } else if (command == "assert_object_destroyed") {
            step.kind = StepKind::AssertObjectDestroyed;
            std::int32_t objectId = -1;
            if (!(tokens >> objectId) || objectId < 0) {
                return invalid(
                    "assert_object_destroyed requires object_id");
            }
            step.objectIds.push_back(objectId);
        } else if (command == "assert_object_hidden") {
            step.kind = StepKind::AssertObjectHidden;
            std::int32_t objectId = -1;
            if (!(tokens >> objectId) || objectId < 0) {
                return invalid("assert_object_hidden requires object_id");
            }
            step.objectIds.push_back(objectId);
        } else if (command == "assert_object_animation") {
            step.kind = StepKind::AssertObjectAnimation;
            std::int32_t objectId = -1;
            if (!(tokens >> objectId >> step.label) || objectId < 0) {
                return invalid(
                    "assert_object_animation requires object_id animation");
            }
            step.objectIds.push_back(objectId);
        } else if (command == "assert_object_electric_state") {
            step.kind = StepKind::AssertObjectElectricState;
            std::int32_t objectId = -1;
            if (!(tokens >> objectId >> step.label) || objectId < 0 ||
                (step.label != "release" && step.label != "off" &&
                 step.label != "warning")) {
                return invalid(
                    "assert_object_electric_state requires object_id "
                    "release|off|warning");
            }
            step.objectIds.push_back(objectId);
        } else if (command == "assert_object_near") {
            step.kind = StepKind::AssertObjectNear;
            std::int32_t objectId = -1;
            if (!(tokens >> objectId >> step.position.x >> step.position.y >>
                  step.position.z >> step.radius) ||
                objectId < 0 || step.radius <= 0.0F) {
                return invalid(
                    "assert_object_near requires object_id x y z radius");
            }
            step.objectIds.push_back(objectId);
        } else if (command == "assert_bonus_collected") {
            step.kind = StepKind::AssertBonusCollected;
            std::int32_t objectId = -1;
            if (!(tokens >> objectId) || objectId < 0) {
                return invalid("assert_bonus_collected requires object_id");
            }
            step.objectIds.push_back(objectId);
        } else if (command == "assert_hostage_freed") {
            step.kind = StepKind::AssertHostageFreed;
            std::int32_t objectId = -1;
            if (!(tokens >> objectId) || objectId < 0) {
                return invalid("assert_hostage_freed requires object_id");
            }
            step.objectIds.push_back(objectId);
        } else if (command == "assert_comic_collected") {
            step.kind = StepKind::AssertComicCollected;
            std::int32_t objectId = -1;
            if (!(tokens >> objectId) || objectId < 0) {
                return invalid("assert_comic_collected requires object_id");
            }
            step.objectIds.push_back(objectId);
        } else if (command == "assert_skill_points_at_least") {
            step.kind = StepKind::AssertSkillPointsAtLeast;
            if (!(tokens >> step.value) || step.value < 0.0F) {
                return invalid(
                    "assert_skill_points_at_least requires a nonnegative value");
            }
        } else if (command == "assert_combo_score_at_least") {
            step.kind = StepKind::AssertComboScoreAtLeast;
            if (!(tokens >> step.value) || step.value < 0.0F) {
                return invalid(
                    "assert_combo_score_at_least requires a nonnegative value");
            }
        } else if (command == "assert_player_state") {
            step.kind = StepKind::AssertPlayerState;
            std::int32_t stateId = -1;
            if (!(tokens >> stateId) || stateId < 0 || stateId > 65535) {
                return invalid("assert_player_state requires state_id");
            }
            step.objectIds.push_back(stateId);
        } else if (command == "assert_player_effect") {
            step.kind = StepKind::AssertPlayerEffect;
            std::int32_t effectId = -1;
            std::int32_t minimumCount = 0;
            if (!(tokens >> effectId >> minimumCount) || effectId < 0 ||
                minimumCount <= 0) {
                return invalid(
                    "assert_player_effect requires effect_id minimum_count");
            }
            step.objectIds.push_back(effectId);
            step.value = static_cast<float>(minimumCount);
        } else if (command == "assert_player_web_line") {
            step.kind = StepKind::AssertPlayerWebLine;
            std::int32_t active = -1;
            std::int32_t count = -1;
            std::int32_t targetObjectId = -1;
            if (!(tokens >> active >> count >> targetObjectId) ||
                (active != 0 && active != 1) || count < 0) {
                return invalid(
                    "assert_player_web_line requires active count target_id");
            }
            step.objectIds = {active, count, targetObjectId};
        } else if (command == "assert_slow_motion") {
            step.kind = StepKind::AssertSlowMotion;
            if (!(tokens >> step.value) || step.value < 1.0F) {
                return invalid(
                    "assert_slow_motion requires a denominator >= 1");
            }
        } else if (command == "assert_gameplay_ui") {
            step.kind = StepKind::AssertGameplayUi;
        } else if (command == "assert_health_above") {
            step.kind = StepKind::AssertHealthAbove;
            if (!(tokens >> step.value)) {
                return invalid("assert_health_above requires a value");
            }
        } else if (command == "assert_health_below") {
            step.kind = StepKind::AssertHealthBelow;
            if (!(tokens >> step.value)) {
                return invalid("assert_health_below requires a value");
            }
        } else if (command == "assert_cinematic_not_started") {
            step.kind = StepKind::AssertCinematicNotStarted;
            std::int32_t cinematicId = -1;
            if (!(tokens >> cinematicId) || cinematicId < 0) {
                return invalid(
                    "assert_cinematic_not_started requires cinematic_id");
            }
            step.objectIds.push_back(cinematicId);
        } else if (command == "assert_death_screen_active") {
            step.kind = StepKind::AssertDeathScreenActive;
        } else if (command == "assert_death_screen_inactive") {
            step.kind = StepKind::AssertDeathScreenInactive;
        } else if (command == "assert_death_alpha_above") {
            step.kind = StepKind::AssertDeathAlphaAbove;
            if (!(tokens >> step.value) || !std::isfinite(step.value)) {
                return invalid("assert_death_alpha_above requires a value");
            }
        } else if (command == "assert_death_confirmation_active") {
            step.kind = StepKind::AssertDeathConfirmationActive;
        } else if (command == "assert_death_confirmation_inactive") {
            step.kind = StepKind::AssertDeathConfirmationInactive;
        } else if (command == "assert_death_confirmation_selection") {
            step.kind = StepKind::AssertDeathConfirmationSelection;
            std::int32_t selection = -1;
            if (!(tokens >> selection) || (selection != 0 && selection != 1)) {
                return invalid(
                    "assert_death_confirmation_selection requires 0 or 1");
            }
            step.objectIds.push_back(selection);
        } else if (command == "assert_audio_played") {
            step.kind = StepKind::AssertAudioPlayed;
            if (!(tokens >> step.label)) {
                return invalid("assert_audio_played requires an event name");
            }
        } else if (command == "assert_audio_not_played") {
            step.kind = StepKind::AssertAudioNotPlayed;
            if (!(tokens >> step.label)) {
                return invalid(
                    "assert_audio_not_played requires an event name");
            }
        } else if (command == "assert_audio_stopped") {
            step.kind = StepKind::AssertAudioStopped;
            if (!(tokens >> step.label)) {
                return invalid("assert_audio_stopped requires an event name");
            }
        } else if (command == "assert_audio_play_count") {
            step.kind = StepKind::AssertAudioPlayCount;
            if (!(tokens >> step.label >>
                  step.durationOrTimeoutMilliseconds) ||
                step.durationOrTimeoutMilliseconds == 0) {
                return invalid(
                    "assert_audio_play_count requires event minimum_count");
            }
        } else if (command == "assert_audio_stop_count") {
            step.kind = StepKind::AssertAudioStopCount;
            if (!(tokens >> step.label >>
                  step.durationOrTimeoutMilliseconds) ||
                step.durationOrTimeoutMilliseconds == 0) {
                return invalid(
                    "assert_audio_stop_count requires event minimum_count");
            }
        } else if (command == "assert_event_not_observed") {
            step.kind = StepKind::AssertEventNotObserved;
            if (!(tokens >> step.label)) {
                return invalid(
                    "assert_event_not_observed requires an event type");
            }
        } else if (command == "finish") {
            step.kind = StepKind::Finish;
        } else {
            return invalid("unknown command " + command);
        }
        std::string trailing;
        if (tokens >> trailing) {
            return invalid("unexpected trailing token " + trailing);
        }
        steps_.push_back(std::move(step));
    }
    if (steps_.empty() || steps_.back().kind != StepKind::Finish) {
        return Result::failure("Autoplay script must end with finish");
    }
    if (startTimeMilliseconds_ >= maximumTimeMilliseconds_) {
        return Result::failure(
            "Autoplay start_time_ms must be less than max_time_ms");
    }
    return Result::success();
}

AutoplayFrameInput AutoplayHarness::update(
    const AutoplaySnapshot& snapshot) {
    lastTimeMilliseconds_ = snapshot.realTimeMilliseconds;
    lastFrameIndex_ = snapshot.frameIndex;
    AutoplayFrameInput input;
    if (complete_ || failed_) {
        lastMotionInput_ = input.motion;
        return input;
    }
    if (snapshot.realTimeMilliseconds > maximumTimeMilliseconds_) {
        failed_ = true;
        failureMessage_ = "autoplay exceeded max_time_ms";
        recordEvent(snapshot.realTimeMilliseconds, "harness_failure",
                    failureMessage_);
        lastMotionInput_ = input.motion;
        return input;
    }
    while (activeStepIndex_ < steps_.size() && !complete_ && !failed_) {
        const Step& step = steps_[activeStepIndex_];
        if (!activeStepStartMilliseconds_) {
            beginStep(snapshot, step);
        }
        AutoplayFrameInput stepInput = updateActiveStep(snapshot, step);
        // Immediate pulse steps can advance to a movement/wait step in this
        // same fixed frame. Accumulate their one-shot actions instead of
        // replacing them with the following step's input (which previously
        // made `jump` followed by `move_to` silently discard the jump).
        input.motion = stepInput.motion;
        input.jumpPressed = input.jumpPressed || stepInput.jumpPressed;
        input.jumpHeld = input.jumpHeld || stepInput.jumpHeld;
        input.jumpReleased = input.jumpReleased || stepInput.jumpReleased;
        input.webPressed = input.webPressed || stepInput.webPressed;
        input.webHeld = input.webHeld || stepInput.webHeld;
        input.webReleased = input.webReleased || stepInput.webReleased;
        input.punchPressed = input.punchPressed || stepInput.punchPressed;
        input.spiderSensePressed =
            input.spiderSensePressed || stepInput.spiderSensePressed;
        input.superAttackPressed =
            input.superAttackPressed || stepInput.superAttackPressed;
        input.quickTimeEventPressed =
            input.quickTimeEventPressed || stepInput.quickTimeEventPressed;
        input.menuUpReleased =
            input.menuUpReleased || stepInput.menuUpReleased;
        input.menuDownReleased =
            input.menuDownReleased || stepInput.menuDownReleased;
        input.menuSelectedReleased =
            input.menuSelectedReleased || stepInput.menuSelectedReleased;
        if (stepInput.teleport.has_value()) {
            input.teleport = stepInput.teleport;
        }
        input.enemyAiOverrides.insert(
            input.enemyAiOverrides.end(), stepInput.enemyAiOverrides.begin(),
            stepInput.enemyAiOverrides.end());
        input.enemyPhysicsOverrides.insert(
            input.enemyPhysicsOverrides.end(),
            stepInput.enemyPhysicsOverrides.begin(),
            stepInput.enemyPhysicsOverrides.end());
        input.enemyDamage.insert(input.enemyDamage.end(),
                                 stepInput.enemyDamage.begin(),
                                 stepInput.enemyDamage.end());
        input.cinematicStartRequests.insert(
            input.cinematicStartRequests.end(),
            stepInput.cinematicStartRequests.begin(),
            stepInput.cinematicStartRequests.end());
        input.captureLabels.insert(input.captureLabels.end(),
                                   stepInput.captureLabels.begin(),
                                   stepInput.captureLabels.end());
        input.quickTimeEventPressed =
            input.quickTimeEventPressed || (autoQuickTimeActions_ &&
            (snapshot.quickTimeEventActive ||
             (snapshot.tutorialVisible && !snapshot.controlsEnabled &&
              step.kind == StepKind::WaitControls)) &&
            step.kind != StepKind::Capture);
        input.punchPressed =
            input.punchPressed ||
            (autoQuickTimeActions_ &&
             snapshot.hostageQuickTimeEventActive &&
             step.kind != StepKind::Capture);
        // A deliberate readback must preserve the current modal prompt for
        // the rendered frame. Automatic acknowledgement resumes on the next
        // step, so capture-based UI regressions can inspect persistent
        // tutorials instead of recording the frame after dismissal.
        const bool immediate = step.kind == StepKind::Jump ||
                               step.kind == StepKind::Punch ||
                               step.kind == StepKind::PressButtons ||
                               step.kind == StepKind::SpiderSense ||
                               step.kind == StepKind::SuperAttack ||
                               step.kind == StepKind::WebOn ||
                               step.kind == StepKind::WebOff ||
                               step.kind == StepKind::SetAutoQuickTime ||
                               step.kind == StepKind::QuickTimeTap ||
                               step.kind == StepKind::MenuUp ||
                               step.kind == StepKind::MenuDown ||
                               step.kind == StepKind::MenuSelect ||
                               step.kind == StepKind::Teleport ||
                               step.kind == StepKind::StartCinematic ||
                               step.kind == StepKind::SetEnemyAi ||
                               step.kind == StepKind::SetEnemyPhysics ||
                               step.kind == StepKind::DamageEnemy ||
                               step.kind == StepKind::Capture;
        if (!immediate || complete_ || failed_) {
            break;
        }
        // Immediate steps must return their one-frame action before advancing
        // into another step, otherwise an edge-triggered input can disappear.
        break;
    }
    lastMotionInput_ = input.motion;
    return input;
}

AutoplayFrameInput AutoplayHarness::updateActiveStep(
    const AutoplaySnapshot& snapshot, const Step& step) {
    AutoplayFrameInput input;
    const std::uint64_t elapsed = snapshot.realTimeMilliseconds -
                                  *activeStepStartMilliseconds_;
    const auto timedOut = [&] {
        return step.durationOrTimeoutMilliseconds != 0 &&
               elapsed > step.durationOrTimeoutMilliseconds;
    };
    switch (step.kind) {
    case StepKind::WaitGameplay:
        if (snapshot.gameplayActive) {
            completeStep(snapshot, step);
        } else if (timedOut()) {
            failStep(snapshot, step, "gameplay did not start before timeout");
        }
        break;
    case StepKind::WaitIntro:
        if (!snapshot.gameplayActive) {
            completeStep(snapshot, step);
        } else if (timedOut()) {
            failStep(snapshot, step, "intro did not start before timeout");
        }
        break;
    case StepKind::WaitControls:
        if (snapshot.gameplayActive && snapshot.controlsEnabled) {
            completeStep(snapshot, step);
        } else if (timedOut()) {
            failStep(snapshot, step,
                     "gameplay controls did not return before timeout");
        }
        break;
    case StepKind::Wait:
        if (elapsed >= step.durationOrTimeoutMilliseconds) {
            completeStep(snapshot, step);
        }
        break;
    case StepKind::MoveTo:
    case StepKind::MoveTo3D:
    case StepKind::ClimbTo: {
        if (!snapshot.gameplayActive) {
            if (timedOut()) {
                failStep(snapshot, step, "move_to timed out before gameplay");
            }
            break;
        }
        const bool horizontallyNear =
            distance2D(snapshot.playerPosition, step.position) <= step.radius;
        const bool verticallyNear =
            std::abs(snapshot.playerPosition.z - step.position.z) <= step.radius;
        if (horizontallyNear &&
            (step.kind == StepKind::MoveTo || verticallyNear)) {
            completeStep(snapshot, step);
        } else if (timedOut()) {
            failStep(snapshot, step,
                     stepName(step.kind) + " did not reach target; position=" +
                         std::to_string(snapshot.playerPosition.x) + "," +
                         std::to_string(snapshot.playerPosition.y) + "," +
                         std::to_string(snapshot.playerPosition.z) +
                         ";target=" + std::to_string(step.position.x) + "," +
                         std::to_string(step.position.y) + "," +
                         std::to_string(step.position.z) +
                         ";state=" + std::to_string(snapshot.playerStateId));
        } else if (snapshot.controlsEnabled && step.kind == StepKind::ClimbTo) {
            // Camera-independent wall-space stick input, never a position
            // write. Pause during attacks/hurt/attach and resume afterward.
            if (snapshot.playerOnWall &&
                (snapshot.playerStateId == 1 || snapshot.playerStateId == 5)) {
                const float dx = step.position.x - snapshot.playerPosition.x;
                const float dy = step.position.y - snapshot.playerPosition.y;
                const float dz = step.position.z - snapshot.playerPosition.z;
                const float side = dx * snapshot.playerFacing.y -
                                   dy * snapshot.playerFacing.x;
                const float distance = std::sqrt(side * side + dz * dz);
                if (distance > 0.001F) {
                    input.motion = {side / distance, dz / distance};
                }
            }
        } else if (snapshot.controlsEnabled && !horizontallyNear) {
            input.motion = steerToward(snapshot, step.position);
        }
        break;
    }
    case StepKind::MoveInput:
        if (elapsed >= step.durationOrTimeoutMilliseconds) {
            completeStep(snapshot, step);
        } else if (snapshot.gameplayActive && snapshot.controlsEnabled) {
            input.motion = {step.position.x, step.position.y};
        }
        break;
    case StepKind::MoveUntilWall:
        if (snapshot.playerOnWall) {
            completeStep(snapshot, step);
        } else if (timedOut()) {
            failStep(snapshot, step, "player did not attach to a wall");
        } else if (snapshot.gameplayActive && snapshot.controlsEnabled) {
            input.motion = steerToward(snapshot, step.position);
        }
        break;
    case StepKind::MoveUntilState:
        if (!step.objectIds.empty() &&
            snapshot.playerStateId == step.objectIds.front()) {
            completeStep(snapshot, step);
        } else if (timedOut()) {
            failStep(snapshot, step,
                     "player did not enter the intended state");
        } else if (snapshot.gameplayActive && snapshot.controlsEnabled) {
            input.motion = {step.position.x, step.position.y};
        }
        break;
    case StepKind::MoveToUntilState:
        if (!step.objectIds.empty() &&
            snapshot.playerStateId == step.objectIds.front()) {
            completeStep(snapshot, step);
        } else if (timedOut()) {
            failStep(snapshot, step,
                     "player did not enter the intended state while steering; "
                     "position=" +
                         std::to_string(snapshot.playerPosition.x) + "," +
                         std::to_string(snapshot.playerPosition.y) + "," +
                         std::to_string(snapshot.playerPosition.z) +
                         ";target=" + std::to_string(step.position.x) + "," +
                         std::to_string(step.position.y) + "," +
                         std::to_string(step.position.z));
        } else if (snapshot.gameplayActive && snapshot.controlsEnabled) {
            input.motion = steerToward(snapshot, step.position);
        }
        break;
    case StepKind::MoveUntilCinematic:
        if (!step.objectIds.empty() &&
            (std::find(snapshot.activeCinematicIds.begin(),
                       snapshot.activeCinematicIds.end(),
                       step.objectIds.front()) !=
                 snapshot.activeCinematicIds.end() ||
             std::find(observedCinematicStarts_.begin(),
                       observedCinematicStarts_.end(),
                       step.objectIds.front()) !=
                 observedCinematicStarts_.end())) {
            completeStep(snapshot, step);
        } else if (timedOut()) {
            failStep(snapshot, step,
                     "intended cinematic did not start");
        } else if (snapshot.gameplayActive && snapshot.controlsEnabled) {
            input.motion = steerToward(snapshot, step.position);
        }
        break;
    case StepKind::MoveInputUntilCinematic:
        if (!step.objectIds.empty() &&
            (std::find(snapshot.activeCinematicIds.begin(),
                       snapshot.activeCinematicIds.end(),
                       step.objectIds.front()) !=
                 snapshot.activeCinematicIds.end() ||
             std::find(observedCinematicStarts_.begin(),
                       observedCinematicStarts_.end(),
                       step.objectIds.front()) !=
                 observedCinematicStarts_.end())) {
            completeStep(snapshot, step);
        } else if (timedOut()) {
            failStep(snapshot, step,
                     "intended cinematic did not start during raw input");
        } else if (snapshot.gameplayActive && snapshot.controlsEnabled) {
            input.motion = {step.position.x, step.position.y};
        }
        break;
    case StepKind::WaitCinematicStarted:
        if (!step.objectIds.empty() &&
            std::find(observedCinematicStarts_.begin(),
                      observedCinematicStarts_.end(),
                      step.objectIds.front()) !=
                observedCinematicStarts_.end()) {
            completeStep(snapshot, step);
        } else if (timedOut()) {
            failStep(snapshot, step,
                     "intended cinematic was not observed");
        }
        break;
    case StepKind::WaitTransportState:
        if (!step.objectIds.empty() &&
            static_cast<std::int32_t>(snapshot.transportState) ==
                step.objectIds.front()) {
            completeStep(snapshot, step);
        } else if (timedOut()) {
            failStep(snapshot, step,
                     "transport did not reach the requested state");
        }
        break;
    case StepKind::WaitDeathScreen:
        if (snapshot.deathScreenActive) {
            completeStep(snapshot, step);
        } else if (timedOut()) {
            failStep(snapshot, step,
                     "native death screen was not observed");
        }
        break;
    case StepKind::WaitDeathConfirmation:
        if (snapshot.deathConfirmationActive) {
            completeStep(snapshot, step);
        } else if (timedOut()) {
            failStep(snapshot, step,
                     "native death confirmation was not observed");
        }
        break;
    case StepKind::WaitExitMenu:
        if (snapshot.exitMenuActive) {
            completeStep(snapshot, step);
        } else if (timedOut()) {
            failStep(snapshot, step,
                     "GS_ExitMenu mode 3 was not observed");
        }
        break;
    case StepKind::WaitMainMenuRequested:
        if (snapshot.mainMenuRequested) {
            completeStep(snapshot, step);
        } else if (timedOut()) {
            failStep(snapshot, step,
                     "GS_MainMenu replacement was not requested");
        }
        break;
    case StepKind::CrossTrigger: {
        const std::int32_t triggerId = step.objectIds[0];
        const std::int32_t cinematicId = step.objectIds[1];
        const bool started =
            std::find(observedCinematicStarts_.begin(),
                      observedCinematicStarts_.end(), cinematicId) !=
                observedCinematicStarts_.end() ||
            std::find(snapshot.activeCinematicIds.begin(),
                      snapshot.activeCinematicIds.end(), cinematicId) !=
                snapshot.activeCinematicIds.end();
        if (started) {
            completeStep(snapshot, step);
            break;
        }
        const auto trigger = std::find_if(
            triggers_.begin(), triggers_.end(),
            [triggerId](const game::LevelTriggerAsset& candidate) {
                return candidate.objectId == triggerId;
            });
        if (trigger == triggers_.end()) {
            failStep(snapshot, step, "trigger asset was not loaded");
            break;
        }
        if (!snapshot.gameplayActive) {
            if (timedOut()) {
                failStep(snapshot, step,
                         "cross_trigger timed out before gameplay");
            }
            break;
        }
        if (timedOut()) {
            failStep(snapshot, step,
                     "trigger did not start the intended cinematic");
            break;
        }
        const auto& matrix = trigger->worldTransform;
        const assets::Vector3 center = matrix[15] != 0.0F
            ? assets::Vector3{matrix[12], matrix[13], matrix[14]}
            : trigger->position;
        const float halfX = std::abs(
            trigger->sizes.x * (trigger->orientedBox ? trigger->scale.x
                                                      : 1.0F)) *
                            0.5F;
        const float halfY = std::abs(
            trigger->sizes.y * (trigger->orientedBox ? trigger->scale.y
                                                      : 1.0F)) *
                            0.5F;
        const float outsideDistance =
            std::sqrt(halfX * halfX + halfY * halfY) + 75.0F;
        // The gameplay position is the player's base. Player::ResetObject
        // (0x0034e258) loads the native 185 cm height from 0x0056ec94.
        const AutoplayTeleport inside{
            {center.x, center.y,
             center.z - game::kPlayerCollisionHalfHeightCentimeters},
            {1.0F, 0.0F, 0.0F}};
        const AutoplayTeleport outside{
            {center.x + outsideDistance, center.y,
             center.z - game::kPlayerCollisionHalfHeightCentimeters},
            {-1.0F, 0.0F, 0.0F}};
        if (activeStepPhase_ == 0) {
            // Prime room visibility and initialize a previously unseen
            // trigger before creating a genuine outside-to-inside edge.
            input.teleport = inside;
        } else if (activeStepPhase_ == 1) {
            input.teleport = outside;
        } else if (activeStepPhase_ == 2) {
            input.teleport = inside;
        }
        if (activeStepPhase_ < 3) {
            ++activeStepPhase_;
        }
        break;
    }
    case StepKind::WaitEnemiesGrounded: {
        const bool allGrounded = std::all_of(
            step.objectIds.begin(), step.objectIds.end(),
            [&snapshot](std::int32_t objectId) {
                const auto match = std::find_if(
                    snapshot.enemies.begin(), snapshot.enemies.end(),
                    [objectId](const game::LevelEnemyState& enemy) {
                        return enemy.asset != nullptr &&
                               enemy.asset->objectId == objectId;
                    });
                return match != snapshot.enemies.end() && match->visible &&
                       match->grounded;
            });
        if (allGrounded) {
            completeStep(snapshot, step);
        } else if (timedOut()) {
            failStep(snapshot, step,
                     "enemies did not become visible and grounded");
        }
        break;
    }
    case StepKind::WaitEnemiesActive: {
        const bool allActive = std::all_of(
            step.objectIds.begin(), step.objectIds.end(),
            [&snapshot](std::int32_t objectId) {
                const auto match = std::find_if(
                    snapshot.enemies.begin(), snapshot.enemies.end(),
                    [objectId](const game::LevelEnemyState& enemy) {
                        return enemy.asset != nullptr &&
                               enemy.asset->objectId == objectId;
                    });
                return match != snapshot.enemies.end() && match->visible &&
                       match->aiEnabled && match->physicsActive &&
                       !match->cinematicMotion.active;
            });
        if (allActive) {
            completeStep(snapshot, step);
        } else if (timedOut()) {
            failStep(snapshot, step,
                     "enemies did not complete cinematic activation");
        }
        break;
    }
    case StepKind::WaitEnemyMeleeAttack: {
        const std::int32_t objectId = step.objectIds.front();
        const auto match = std::find_if(
            snapshot.enemies.begin(), snapshot.enemies.end(),
            [objectId](const game::LevelEnemyState& enemy) {
                return enemy.asset != nullptr &&
                       enemy.asset->objectId == objectId;
            });
        if (match != snapshot.enemies.end() && match->visible &&
            match->aiEnabled && match->meleeAttackRegistered &&
            match->meleeAttackActive) {
            completeStep(snapshot, step);
        } else if (timedOut()) {
            failStep(snapshot, step,
                     "enemy did not register and begin a melee attack");
        }
        break;
    }
    case StepKind::WaitEnemyProjectile: {
        const bool molotovPresent = std::any_of(
            snapshot.molotovs.begin(), snapshot.molotovs.end(),
            [&step](const game::EnemyMolotovState& molotov) {
                return molotov.active && !step.objectIds.empty() &&
                       molotov.sourceObjectId == step.objectIds.front();
            });
        const bool boomerangPresent = std::any_of(
            snapshot.boomerangs.begin(), snapshot.boomerangs.end(),
            [&step](const game::EnemyBoomerangState& boomerang) {
                return boomerang.active && !step.objectIds.empty() &&
                       boomerang.sourceObjectId == step.objectIds.front();
            });
        const bool thunderclapPresent = std::any_of(
            snapshot.thunderclaps.begin(), snapshot.thunderclaps.end(),
            [&step](const game::EnemyThunderclapState& thunderclap) {
                return thunderclap.active && !step.objectIds.empty() &&
                       thunderclap.sourceObjectId == step.objectIds.front();
            });
        const bool electricPostPresent = std::any_of(
            snapshot.electricPosts.begin(), snapshot.electricPosts.end(),
            [&step](const game::EnemyElectricPostState& post) {
                return post.active && !step.objectIds.empty() &&
                       post.sourceObjectId == step.objectIds.front();
            });
        if (molotovPresent || boomerangPresent || thunderclapPresent ||
            electricPostPresent) {
            completeStep(snapshot, step);
        } else if (timedOut()) {
            failStep(snapshot, step,
                     "enemy projectile was not observed before timeout");
        }
        break;
    }
    case StepKind::SetEnemyAi:
        input.enemyAiOverrides.push_back(
            {step.objectIds.front(), step.value != 0.0F,
             step.radius != 0.0F});
        completeStep(snapshot, step);
        break;
    case StepKind::SetEnemyPhysics:
        input.enemyPhysicsOverrides.push_back(
            {step.objectIds.front(), step.value != 0.0F});
        completeStep(snapshot, step);
        break;
    case StepKind::DamageEnemy:
        input.enemyDamage.push_back({step.objectIds.front(), step.value});
        completeStep(snapshot, step);
        break;
    case StepKind::WaitEnemyAnimation: {
        const std::int32_t objectId = step.objectIds.front();
        const auto enemy = std::find_if(
            snapshot.enemies.begin(), snapshot.enemies.end(),
            [objectId](const game::LevelEnemyState& state) {
                return state.asset != nullptr &&
                       state.asset->objectId == objectId;
            });
        if (enemy == snapshot.enemies.end()) {
            failStep(snapshot, step, "enemy was not loaded");
        } else if (enemy->activeAnimation == step.label) {
            completeStep(snapshot, step);
        } else if (timedOut()) {
            failStep(snapshot, step,
                     "enemy did not enter animation " + step.label +
                         "; current=" + enemy->activeAnimation);
        }
        break;
    }
    case StepKind::WaitObjectElectricState: {
        const std::int32_t objectId = step.objectIds.front();
        const auto object = std::find_if(
            snapshot.objects.begin(), snapshot.objects.end(),
            [objectId](const game::LevelObjectState& state) {
                return state.asset != nullptr &&
                       state.asset->objectId == objectId;
            });
        if (object == snapshot.objects.end() || object->asset->kind !=
                                                    game::LevelObjectKind::
                                                        ElectricPlatform) {
            failStep(snapshot, step, "electric platform was not loaded");
            break;
        }
        std::string_view actual = "off";
        switch (object->electricState) {
        case game::ElectricPlatformState::Release: actual = "release"; break;
        case game::ElectricPlatformState::Off: actual = "off"; break;
        case game::ElectricPlatformState::Warning: actual = "warning"; break;
        }
        if (actual == step.label) {
            completeStep(snapshot, step);
        } else if (timedOut()) {
            failStep(snapshot, step,
                     "electric platform did not enter state " + step.label +
                         "; current=" + std::string(actual));
        }
        break;
    }
    case StepKind::WaitAreaDamageState: {
        const std::int32_t objectId = step.objectIds.front();
        const auto object = std::find_if(
            snapshot.objects.begin(), snapshot.objects.end(),
            [objectId](const game::LevelObjectState& state) {
                return state.asset != nullptr &&
                       state.asset->objectId == objectId;
            });
        if (object == snapshot.objects.end() || object->asset->kind !=
                                                    game::LevelObjectKind::
                                                        AreaDamage) {
            failStep(snapshot, step, "area-damage object was not loaded");
            break;
        }
        std::string_view actual = "delay";
        if (object->areaDamageState == 0) {
            actual = "active";
        } else if (object->areaDamageState == 1) {
            actual = "waiting";
        }
        if (actual == step.label) {
            completeStep(snapshot, step);
        } else if (timedOut()) {
            failStep(snapshot, step,
                     "area-damage object did not enter state " + step.label +
                         "; current=" + std::string(actual));
        }
        break;
    }
    case StepKind::WaitObjectNear: {
        const std::int32_t objectId = step.objectIds.front();
        const auto object = std::find_if(
            snapshot.objects.begin(), snapshot.objects.end(),
            [objectId](const game::LevelObjectState& state) {
                return state.asset != nullptr &&
                       state.asset->objectId == objectId;
            });
        if (object == snapshot.objects.end()) {
            failStep(snapshot, step, "object was not loaded");
            break;
        }
        const float dx = object->position.x - step.position.x;
        const float dy = object->position.y - step.position.y;
        const float dz = object->position.z - step.position.z;
        if (std::sqrt(dx * dx + dy * dy + dz * dz) <= step.radius) {
            completeStep(snapshot, step);
        } else if (timedOut()) {
            failStep(snapshot, step,
                     "object did not enter wait_object_near radius");
        }
        break;
    }
    case StepKind::WaitLevelEnd:
        if (snapshot.levelEnded) {
            completeStep(snapshot, step);
        } else if (timedOut()) {
            failStep(snapshot, step,
                     "level end command was not observed before timeout");
        }
        break;
    case StepKind::Attack: {
        if (snapshot.playerStateId == 87) {
            activeAttackFarStateObserved_ = true;
        }
        const game::LevelEnemyState* target = nullptr;
        float targetDistance = std::numeric_limits<float>::max();
        bool anyAlive = false;
        for (const std::int32_t objectId : step.objectIds) {
            const auto match = std::find_if(
                snapshot.enemies.begin(), snapshot.enemies.end(),
                [objectId](const game::LevelEnemyState& enemy) {
                    return enemy.asset != nullptr &&
                           enemy.asset->objectId == objectId;
                });
            if (match == snapshot.enemies.end() || match->health <= 0.0F) {
                continue;
            }
            anyAlive = true;
            const float playerTop =
                snapshot.playerPosition.z +
                game::kPlayerCollisionHeightCentimeters;
            const float enemyTop = match->position.z +
                                   match->collisionHeight;
            if (!match->visible ||
                (snapshot.playerOnWall ? !match->onWall :
                 (playerTop < match->position.z ||
                  enemyTop < snapshot.playerPosition.z))) {
                continue;
            }
            const float horizontalDistance =
                distance2D(snapshot.playerPosition, match->position);
            const float candidateDistance = snapshot.playerOnWall
                ? std::hypot(horizontalDistance,
                             match->position.z - snapshot.playerPosition.z)
                : horizontalDistance;
            if (candidateDistance < targetDistance) {
                target = &*match;
                targetDistance = candidateDistance;
            }
        }
        if (!anyAlive) {
            if (!snapshot.playerOnWall && step.radius > 250.0F &&
                !activeAttackFarStateObserved_) {
                failStep(snapshot, step,
                         "far target died without entering attack state 87");
            } else {
                completeStep(snapshot, step);
            }
        } else if (timedOut()) {
            failStep(snapshot, step, "attack targets remained alive");
        } else if (target != nullptr && snapshot.controlsEnabled) {
            if (snapshot.playerOnWall) {
                // Process-local input only: wall controls are lateral/up,
                // not ground-camera X/Y steering. Wait out hit reactions
                // and animation completion before trying the next punch.
                if (targetDistance <= step.radius) {
                    input.punchPressed = snapshot.playerPunchTransitionReady;
                } else if (snapshot.playerStateId == 1 ||
                           snapshot.playerStateId == 5) {
                    const float dx = target->position.x - snapshot.playerPosition.x;
                    const float dy = target->position.y - snapshot.playerPosition.y;
                    const float dz = target->position.z - snapshot.playerPosition.z;
                    const float side = dx * snapshot.playerFacing.y -
                                       dy * snapshot.playerFacing.x;
                    const float length = std::hypot(side, dz);
                    if (length > 0.001F) {
                        input.motion = {side / length, dz / length};
                    }
                }
                break;
            }
            input.motion = steerToward(snapshot, target->position);
            if (targetDistance <= step.radius) {
                if (snapshot.playerPunchTransitionReady) {
                    // Preserve the goal-directed stick vector. Native attack
                    // acquisition uses it to choose and face the intended
                    // target; requiring the old facing to be nearly exact
                    // made the driver miss short combo input windows.
                    input.punchPressed = true;
                } else {
                    input.motion.right *= 0.25F;
                    input.motion.forward *= 0.25F;
                }
            }
        }
        break;
    }
    case StepKind::AttackObject: {
        const std::int32_t objectId = step.objectIds.front();
        const auto target = std::find_if(
            snapshot.objects.begin(), snapshot.objects.end(),
            [objectId](const game::LevelObjectState& object) {
                return object.asset != nullptr &&
                       object.asset->objectId == objectId;
            });
        if (target == snapshot.objects.end()) {
            failStep(snapshot, step, "attack object was not loaded");
        } else if (target->destructionPhase ==
                   game::LevelObjectDestructionPhase::Destroyed) {
            completeStep(snapshot, step);
        } else if (timedOut()) {
            failStep(snapshot, step, "attack object remained intact");
        } else if (!target->visible) {
            failStep(snapshot, step, "attack object was not visible");
        } else if (snapshot.controlsEnabled) {
            const float targetDistance =
                distance2D(snapshot.playerPosition, target->position);
            input.motion = steerToward(snapshot, target->position);
            if (targetDistance <= step.radius) {
                if (snapshot.playerPunchTransitionReady) {
                    input.punchPressed = true;
                } else {
                    input.motion.right *= 0.25F;
                    input.motion.forward *= 0.25F;
                }
            }
        }
        break;
    }
    case StepKind::CollectBonus: {
        const std::int32_t objectId = step.objectIds.front();
        const auto bonus = std::find_if(
            snapshot.bonuses.begin(), snapshot.bonuses.end(),
            [objectId](const game::LevelBonusState& state) {
                return state.asset != nullptr &&
                       state.asset->objectId == objectId;
            });
        if (bonus == snapshot.bonuses.end()) {
            failStep(snapshot, step, "bonus was not loaded");
        } else if (!bonus->visible && !bonus->orbActive) {
            completeStep(snapshot, step);
        } else if (timedOut()) {
            failStep(snapshot, step,
                     bonus->orbActive ? "bonus orb did not finish"
                                      : "bonus was not collected");
        } else if (bonus->visible && snapshot.controlsEnabled) {
            // CBonus targets the player's upper body, 100 units above the
            // collision origin, so its authored point is the correct 2D
            // navigation target here.
            input.motion = steerToward(snapshot, bonus->asset->position);
        }
        break;
    }
    case StepKind::RescueHostage: {
        const std::int32_t objectId = step.objectIds.front();
        const auto hostage = std::find_if(
            snapshot.hostages.begin(), snapshot.hostages.end(),
            [objectId](const game::LevelHostageState& state) {
                return state.asset != nullptr &&
                       state.asset->objectId == objectId;
            });
        const game::HostageRescuePhase targetPhase =
            step.label == "qte" ? game::HostageRescuePhase::QuickTime
                                : game::HostageRescuePhase::Freed;
        if (hostage == snapshot.hostages.end()) {
            failStep(snapshot, step, "hostage was not loaded");
        } else if (hostage->phase == targetPhase) {
            completeStep(snapshot, step);
        } else if (timedOut()) {
            failStep(snapshot, step,
                     "hostage rescue did not finish; phase=" +
                         std::to_string(static_cast<std::int32_t>(
                             hostage->phase)) +
                         ";actions=" +
                         std::to_string(hostage->completedActions) + "/" +
                         std::to_string(hostage->requiredActions));
        } else if (hostage->phase == game::HostageRescuePhase::Tied) {
            if (hostage->promptVisible) {
                input.punchPressed = true;
            } else if (snapshot.controlsEnabled) {
                input.motion = steerToward(snapshot,
                                           hostage->asset->position);
            }
        } else if (hostage->phase ==
                       game::HostageRescuePhase::QuickTime &&
                   autoQuickTimeActions_) {
            input.punchPressed = true;
        }
        break;
    }
    case StepKind::WaitDropHit: {
        const std::int32_t objectId = step.objectIds.front();
        const auto drop = std::find_if(
            snapshot.drops.begin(), snapshot.drops.end(),
            [objectId](const game::LevelDropObjectState& state) {
                return state.asset != nullptr &&
                       state.asset->objectId == objectId;
            });
        if (drop == snapshot.drops.end()) {
            failStep(snapshot, step, "drop object was not loaded");
        } else if (drop->hitPlayer) {
            completeStep(snapshot, step);
        } else if (timedOut()) {
            failStep(snapshot, step,
                     "drop object did not hit the player; phase=" +
                         std::to_string(static_cast<std::int32_t>(
                             drop->phase)));
        }
        break;
    }
    case StepKind::CollectComic: {
        const std::int32_t objectId = step.objectIds.front();
        const auto comic = std::find_if(
            snapshot.objects.begin(), snapshot.objects.end(),
            [objectId](const game::LevelObjectState& state) {
                return state.asset != nullptr &&
                       state.asset->objectId == objectId &&
                       state.asset->kind == game::LevelObjectKind::Comic;
            });
        if (comic == snapshot.objects.end()) {
            failStep(snapshot, step, "comic cover was not loaded");
        } else if (comic->comicCollected) {
            completeStep(snapshot, step);
        } else if (timedOut()) {
            failStep(snapshot, step, "comic cover was not collected");
        } else if (!comic->visible) {
            failStep(snapshot, step,
                     "uncollected comic cover was not visible");
        } else if (snapshot.controlsEnabled) {
            input.motion = steerToward(snapshot, comic->position);
        }
        break;
    }
    case StepKind::Jump:
        // A damage reaction retains the ordinary idle state id while its
        // non-interruptible hurt clip is still playing.  Do not consume the
        // one-frame jump pulse until the production controller can accept it.
        if (!snapshot.gameplayActive || !snapshot.controlsEnabled ||
            snapshot.deathScreenActive || snapshot.restoreActive ||
            snapshot.playerAnimation.find("hurt") != std::string_view::npos) {
            break;
        }
        input.motion = {step.position.x, step.position.y};
        input.jumpPressed = true;
        input.jumpHeld = true;
        completeStep(snapshot, step);
        break;
    case StepKind::Punch:
        input.punchPressed = true;
        completeStep(snapshot, step);
        break;
    case StepKind::PressButtons:
        input.jumpPressed =
            std::ranges::find(step.objectIds, 8) != step.objectIds.end();
        input.jumpHeld = input.jumpPressed;
        input.punchPressed =
            std::ranges::find(step.objectIds, 6) != step.objectIds.end();
        input.webPressed =
            std::ranges::find(step.objectIds, 5) != step.objectIds.end();
        input.webHeld = input.webPressed;
        completeStep(snapshot, step);
        break;
    case StepKind::PunchWhenReady:
        if (snapshot.gameplayActive && snapshot.controlsEnabled &&
            snapshot.playerPunchTransitionReady) {
            input.punchPressed = true;
            completeStep(snapshot, step);
        } else if (timedOut()) {
            failStep(snapshot, step,
                     "player punch transition window did not open");
        }
        break;
    case StepKind::PunchAttackWhenReady:
        if (snapshot.gameplayActive && snapshot.controlsEnabled &&
            snapshot.playerPunchAttackTransitionReady) {
            input.punchPressed = true;
            completeStep(snapshot, step);
        } else if (timedOut()) {
            failStep(snapshot, step,
                     "player punch attack transition window did not open");
        }
        break;
    case StepKind::JumpAttackWhenReady:
        if (snapshot.gameplayActive && snapshot.controlsEnabled &&
            snapshot.playerJumpAttackTransitionReady) {
            // This command waits for GameplayPlayer's predicate-103
            // transition, which is the native held-button path. Emitting a
            // fresh press as well makes Application's phase arbitration pick
            // Pressed and correctly reject that transition.
            input.jumpHeld = true;
            completeStep(snapshot, step);
        } else if (timedOut()) {
            failStep(snapshot, step,
                     "player jump attack transition window did not open");
        }
        break;
    case StepKind::JumpReleaseAttackWhenReady:
        if (snapshot.gameplayActive && snapshot.controlsEnabled &&
            snapshot.playerJumpReleaseAttackTransitionReady) {
            input.jumpReleased = true;
            completeStep(snapshot, step);
        } else if (timedOut()) {
            failStep(snapshot, step,
                     "player jump-release attack transition window did not open");
        }
        break;
    case StepKind::WebAttackWhenReady:
        if (snapshot.gameplayActive && snapshot.controlsEnabled &&
            snapshot.playerWebAttackTransitionReady) {
            input.webPressed = true;
            completeStep(snapshot, step);
        } else if (timedOut()) {
            failStep(snapshot, step,
                     "player web attack transition window did not open");
        }
        break;
    case StepKind::WebHeldAttackWhenReady:
        if (snapshot.gameplayActive && snapshot.controlsEnabled &&
            snapshot.playerWebHeldAttackTransitionReady) {
            input.webHeld = true;
            completeStep(snapshot, step);
        } else if (timedOut()) {
            failStep(snapshot, step,
                     "player held-web attack transition window did not open");
        }
        break;
    case StepKind::SpiderSense:
        input.spiderSensePressed = true;
        completeStep(snapshot, step);
        break;
    case StepKind::SuperAttack:
        input.superAttackPressed = true;
        completeStep(snapshot, step);
        break;
    case StepKind::WebOn:
        input.webPressed = true;
        input.webHeld = true;
        completeStep(snapshot, step);
        break;
    case StepKind::WebOff:
        input.webReleased = true;
        completeStep(snapshot, step);
        break;
    case StepKind::SetAutoQuickTime:
        autoQuickTimeActions_ = step.value != 0.0F;
        completeStep(snapshot, step);
        break;
    case StepKind::QuickTimeTap:
        input.quickTimeEventPressed = true;
        completeStep(snapshot, step);
        break;
    case StepKind::MenuUp:
        input.menuUpReleased = true;
        completeStep(snapshot, step);
        break;
    case StepKind::MenuDown:
        input.menuDownReleased = true;
        completeStep(snapshot, step);
        break;
    case StepKind::MenuSelect:
        input.menuSelectedReleased = true;
        completeStep(snapshot, step);
        break;
    case StepKind::Teleport:
        input.teleport = AutoplayTeleport{step.position, step.facing};
        completeStep(snapshot, step);
        break;
    case StepKind::StartCinematic:
        input.cinematicStartRequests.push_back(step.objectIds.front());
        completeStep(snapshot, step);
        break;
    case StepKind::Capture:
        input.captureLabels.push_back(step.label);
        completeStep(snapshot, step);
        break;
    case StepKind::AssertNear:
        if (distance2D(snapshot.playerPosition, step.position) > step.radius ||
            std::abs(snapshot.playerPosition.z - step.position.z) >
                step.radius) {
            failStep(snapshot, step, "player is outside assert_near radius");
        } else {
            completeStep(snapshot, step);
        }
        break;
    case StepKind::AssertCameraArea:
        if (step.objectIds.empty() ||
            snapshot.cameraAreaId != step.objectIds.front()) {
            failStep(snapshot, step,
                     "active camera area differs from expected");
        } else {
            completeStep(snapshot, step);
        }
        break;
    case StepKind::AssertLastCheckPoint:
        if (step.objectIds.empty() ||
            snapshot.lastCheckPointId != step.objectIds.front()) {
            failStep(snapshot, step,
                     "last checkpoint differs from expected");
        } else {
            completeStep(snapshot, step);
        }
        break;
    case StepKind::AssertEnemyNear: {
        const std::int32_t objectId = step.objectIds.front();
        const auto match = std::find_if(
            snapshot.enemies.begin(), snapshot.enemies.end(),
            [objectId](const game::LevelEnemyState& enemy) {
                return enemy.asset != nullptr &&
                       enemy.asset->objectId == objectId;
            });
        if (match == snapshot.enemies.end()) {
            failStep(snapshot, step, "enemy was not loaded");
        } else {
            const float dx = match->position.x - step.position.x;
            const float dy = match->position.y - step.position.y;
            const float dz = match->position.z - step.position.z;
            if (std::sqrt(dx * dx + dy * dy + dz * dz) > step.radius) {
                failStep(snapshot, step,
                         "enemy is outside assert_enemy_near radius");
            } else {
                completeStep(snapshot, step);
            }
        }
        break;
    }
    case StepKind::AssertEnemyHealthBelow: {
        const std::int32_t objectId = step.objectIds.front();
        const auto enemy = std::find_if(
            snapshot.enemies.begin(), snapshot.enemies.end(),
            [objectId](const game::LevelEnemyState& state) {
                return state.asset != nullptr &&
                       state.asset->objectId == objectId;
            });
        if (enemy == snapshot.enemies.end()) {
            failStep(snapshot, step, "enemy was not loaded");
        } else if (enemy->health >= step.value) {
            failStep(snapshot, step,
                     "enemy health did not satisfy "
                     "assert_enemy_health_below; actual=" +
                         std::to_string(enemy->health));
        } else {
            completeStep(snapshot, step);
        }
        break;
    }
    case StepKind::AssertEnemyHealthNear: {
        const std::int32_t objectId = step.objectIds.front();
        const auto enemy = std::find_if(
            snapshot.enemies.begin(), snapshot.enemies.end(),
            [objectId](const game::LevelEnemyState& state) {
                return state.asset != nullptr &&
                       state.asset->objectId == objectId;
            });
        if (enemy == snapshot.enemies.end()) {
            failStep(snapshot, step, "enemy was not loaded");
        } else if (std::abs(enemy->health - step.value) > step.radius) {
            failStep(snapshot, step,
                     "enemy health did not satisfy "
                     "assert_enemy_health_near; expected=" +
                         std::to_string(step.value) + ";tolerance=" +
                         std::to_string(step.radius) + ";actual=" +
                         std::to_string(enemy->health));
        } else {
            completeStep(snapshot, step);
        }
        break;
    }
    case StepKind::AssertEnemyMeleeAttackActive: {
        const std::int32_t objectId = step.objectIds.front();
        const bool expected = step.objectIds[1] != 0;
        const auto enemy = std::find_if(
            snapshot.enemies.begin(), snapshot.enemies.end(),
            [objectId](const game::LevelEnemyState& state) {
                return state.asset != nullptr &&
                       state.asset->objectId == objectId;
            });
        if (enemy == snapshot.enemies.end()) {
            failStep(snapshot, step, "enemy was not loaded");
        } else if (enemy->meleeAttackActive != expected) {
            failStep(snapshot, step,
                     "enemy melee-attack active flag differed; expected=" +
                         std::to_string(expected ? 1 : 0) + ";actual=" +
                         std::to_string(enemy->meleeAttackActive ? 1 : 0));
        } else {
            completeStep(snapshot, step);
        }
        break;
    }
    case StepKind::AssertEnemyBehavior: {
        const std::int32_t objectId = step.objectIds.front();
        const auto enemy = std::find_if(
            snapshot.enemies.begin(), snapshot.enemies.end(),
            [objectId](const game::LevelEnemyState& state) {
                return state.asset != nullptr &&
                       state.asset->objectId == objectId;
            });
        if (enemy == snapshot.enemies.end()) {
            failStep(snapshot, step, "enemy was not loaded");
        } else if (behaviorName(enemy->behavior) != step.label) {
            failStep(snapshot, step,
                     "enemy behavior differed; expected=" + step.label +
                         ";actual=" + behaviorName(enemy->behavior));
        } else {
            completeStep(snapshot, step);
        }
        break;
    }
    case StepKind::AssertEnemyDistanceAbove: {
        const auto findEnemy = [&](std::int32_t objectId) {
            return std::find_if(
                snapshot.enemies.begin(), snapshot.enemies.end(),
                [objectId](const game::LevelEnemyState& enemy) {
                    return enemy.asset != nullptr &&
                           enemy.asset->objectId == objectId;
                });
        };
        const auto first = findEnemy(step.objectIds[0]);
        const auto second = findEnemy(step.objectIds[1]);
        if (first == snapshot.enemies.end() ||
            second == snapshot.enemies.end()) {
            failStep(snapshot, step, "enemy was not loaded");
        } else {
            const float distance = std::hypot(
                second->position.x - first->position.x,
                second->position.y - first->position.y);
            if (distance + 0.01F < step.value) {
                failStep(snapshot, step,
                         "enemy distance did not satisfy "
                         "assert_enemy_distance_above; actual=" +
                             std::to_string(distance));
            } else {
                completeStep(snapshot, step);
            }
        }
        break;
    }
    case StepKind::AssertObjectDestroyed: {
        const std::int32_t objectId = step.objectIds.front();
        const auto match = std::find_if(
            snapshot.objects.begin(), snapshot.objects.end(),
            [objectId](const game::LevelObjectState& object) {
                return object.asset != nullptr &&
                       object.asset->objectId == objectId;
            });
        if (match == snapshot.objects.end()) {
            failStep(snapshot, step, "object was not loaded");
        } else if (match->destructionPhase !=
                   game::LevelObjectDestructionPhase::Destroyed) {
            failStep(snapshot, step,
                     "object had not reached its terminal broken pose");
        } else {
            completeStep(snapshot, step);
        }
        break;
    }
    case StepKind::AssertObjectHidden:
    case StepKind::AssertObjectAnimation: {
        const std::int32_t objectId = step.objectIds.front();
        const auto match = std::find_if(
            snapshot.objects.begin(), snapshot.objects.end(),
            [objectId](const game::LevelObjectState& object) {
                return object.asset != nullptr &&
                       object.asset->objectId == objectId;
            });
        if (match == snapshot.objects.end()) {
            failStep(snapshot, step, "object was not loaded");
        } else if (step.kind == StepKind::AssertObjectHidden &&
                   match->visible) {
            failStep(snapshot, step, "object remained visible");
        } else if (step.kind == StepKind::AssertObjectAnimation &&
                   match->activeAnimation != step.label) {
            failStep(snapshot, step,
                     "object animation was " + match->activeAnimation +
                         ", expected " + step.label);
        } else {
            completeStep(snapshot, step);
        }
        break;
    }
    case StepKind::AssertObjectElectricState: {
        const std::int32_t objectId = step.objectIds.front();
        const auto match = std::find_if(
            snapshot.objects.begin(), snapshot.objects.end(),
            [objectId](const game::LevelObjectState& object) {
                return object.asset != nullptr &&
                       object.asset->objectId == objectId;
            });
        if (match == snapshot.objects.end() || match->asset->kind !=
                                                   game::LevelObjectKind::
                                                       ElectricPlatform) {
            failStep(snapshot, step, "electric platform was not loaded");
            break;
        }
        std::string_view actual = "off";
        switch (match->electricState) {
        case game::ElectricPlatformState::Release: actual = "release"; break;
        case game::ElectricPlatformState::Off: actual = "off"; break;
        case game::ElectricPlatformState::Warning: actual = "warning"; break;
        }
        if (actual != step.label) {
            failStep(snapshot, step,
                     "electric platform state was " + std::string(actual) +
                         ", expected " + step.label);
        } else {
            completeStep(snapshot, step);
        }
        break;
    }
    case StepKind::AssertObjectNear: {
        const std::int32_t objectId = step.objectIds.front();
        const auto match = std::find_if(
            snapshot.objects.begin(), snapshot.objects.end(),
            [objectId](const game::LevelObjectState& object) {
                return object.asset != nullptr &&
                       object.asset->objectId == objectId;
            });
        if (match == snapshot.objects.end()) {
            failStep(snapshot, step, "object was not loaded");
            break;
        }
        const float dx = match->position.x - step.position.x;
        const float dy = match->position.y - step.position.y;
        const float dz = match->position.z - step.position.z;
        if (std::sqrt(dx * dx + dy * dy + dz * dz) > step.radius) {
            failStep(snapshot, step,
                     "object is outside assert_object_near radius");
        } else {
            completeStep(snapshot, step);
        }
        break;
    }
    case StepKind::AssertComicCollected: {
        const std::int32_t objectId = step.objectIds.front();
        const auto comic = std::find_if(
            snapshot.objects.begin(), snapshot.objects.end(),
            [objectId](const game::LevelObjectState& state) {
                return state.asset != nullptr &&
                       state.asset->objectId == objectId &&
                       state.asset->kind == game::LevelObjectKind::Comic;
            });
        if (comic == snapshot.objects.end()) {
            failStep(snapshot, step, "comic cover was not loaded");
        } else if (!comic->comicCollected || comic->visible) {
            failStep(snapshot, step,
                     "comic cover collection state was incomplete");
        } else {
            completeStep(snapshot, step);
        }
        break;
    }
    case StepKind::AssertBonusCollected: {
        const std::int32_t objectId = step.objectIds.front();
        const auto bonus = std::find_if(
            snapshot.bonuses.begin(), snapshot.bonuses.end(),
            [objectId](const game::LevelBonusState& state) {
                return state.asset != nullptr &&
                       state.asset->objectId == objectId;
            });
        if (bonus == snapshot.bonuses.end()) {
            failStep(snapshot, step, "bonus was not loaded");
        } else if (bonus->visible || bonus->orbActive) {
            failStep(snapshot, step, "bonus pickup lifecycle was incomplete");
        } else {
            completeStep(snapshot, step);
        }
        break;
    }
    case StepKind::AssertHostageFreed: {
        const std::int32_t objectId = step.objectIds.front();
        const auto hostage = std::find_if(
            snapshot.hostages.begin(), snapshot.hostages.end(),
            [objectId](const game::LevelHostageState& state) {
                return state.asset != nullptr &&
                       state.asset->objectId == objectId;
            });
        if (hostage == snapshot.hostages.end()) {
            failStep(snapshot, step, "hostage was not loaded");
        } else if (hostage->phase != game::HostageRescuePhase::Freed) {
            failStep(snapshot, step,
                     "hostage was not in the freed state");
        } else {
            completeStep(snapshot, step);
        }
        break;
    }
    case StepKind::AssertSkillPointsAtLeast:
        if (snapshot.playerSkillPoints <
            static_cast<std::int32_t>(step.value)) {
            failStep(snapshot, step,
                     "player skill point total was below the assertion");
        } else {
            completeStep(snapshot, step);
        }
        break;
    case StepKind::AssertComboScoreAtLeast:
        if (snapshot.playerComboScore <
            static_cast<std::int32_t>(step.value)) {
            failStep(snapshot, step,
                     "player combo score was below the assertion");
        } else {
            completeStep(snapshot, step);
        }
        break;
    case StepKind::AssertPlayerState:
        if (step.objectIds.empty() ||
            snapshot.playerStateId != step.objectIds.front()) {
            failStep(snapshot, step,
                     "player state differed from assertion; actual=" +
                         std::to_string(snapshot.playerStateId));
        } else {
            completeStep(snapshot, step);
        }
        break;
    case StepKind::AssertPlayerEffect: {
        const std::int16_t effectId = static_cast<std::int16_t>(
            step.objectIds.front());
        const std::size_t count = static_cast<std::size_t>(std::count_if(
            snapshot.playerHitEffects.begin(),
            snapshot.playerHitEffects.end(),
            [effectId](const game::PlayerHitEffectState& effect) {
                return effect.effectId == effectId;
            }));
        if (count < static_cast<std::size_t>(step.value)) {
            failStep(snapshot, step,
                     "player effect count was below assertion; effect=" +
                         std::to_string(effectId) +
                         ";actual=" + std::to_string(count));
        } else {
            completeStep(snapshot, step);
        }
        break;
    }
    case StepKind::AssertPlayerWebLine: {
        const bool expectedActive = step.objectIds[0] != 0;
        const std::size_t expectedCount =
            static_cast<std::size_t>(step.objectIds[1]);
        const std::int32_t expectedTarget = step.objectIds[2];
        if (snapshot.playerWebLineActive != expectedActive ||
            snapshot.playerWebLineCount != expectedCount ||
            snapshot.playerWebLineTargetObjectId != expectedTarget) {
            failStep(
                snapshot, step,
                "player web-line state differed; expected_active=" +
                    std::to_string(expectedActive ? 1 : 0) +
                    ";expected_count=" + std::to_string(expectedCount) +
                    ";expected_target=" + std::to_string(expectedTarget) +
                    ";actual_active=" +
                    std::to_string(snapshot.playerWebLineActive ? 1 : 0) +
                    ";actual_count=" +
                    std::to_string(snapshot.playerWebLineCount) +
                    ";actual_target=" +
                    std::to_string(snapshot.playerWebLineTargetObjectId));
        } else {
            completeStep(snapshot, step);
        }
        break;
    }
    case StepKind::AssertSlowMotion:
        if (std::abs(snapshot.slowMotionDenominator - step.value) > 0.001F) {
            failStep(snapshot, step,
                     "slow-motion denominator differed from assertion; "
                     "actual=" +
                         std::to_string(snapshot.slowMotionDenominator));
        } else {
            completeStep(snapshot, step);
        }
        break;
    case StepKind::AssertGameplayUi:
        if (!snapshot.gameplayActive || !snapshot.controlsEnabled ||
            !snapshot.attributionEnabled || snapshot.cinematicLetterboxVisible) {
            failStep(snapshot, step,
                     "gameplay input/HUD/cinematic-band handoff is incomplete");
        } else {
            completeStep(snapshot, step);
        }
        break;
    case StepKind::AssertHealthAbove:
        if (snapshot.playerHealth <= step.value) {
            failStep(snapshot, step,
                     "player health did not satisfy assert_health_above");
        } else {
            completeStep(snapshot, step);
        }
        break;
    case StepKind::AssertHealthBelow:
        if (snapshot.playerHealth >= step.value) {
            failStep(snapshot, step,
                     "player health did not satisfy assert_health_below");
        } else {
            completeStep(snapshot, step);
        }
        break;
    case StepKind::AssertCinematicNotStarted:
        if (std::find(observedCinematicStarts_.begin(),
                      observedCinematicStarts_.end(),
                      step.objectIds.front()) !=
            observedCinematicStarts_.end()) {
            failStep(snapshot, step,
                     "cinematic was unexpectedly observed");
        } else {
            completeStep(snapshot, step);
        }
        break;
    case StepKind::AssertDeathScreenActive:
        if (!snapshot.deathScreenActive) {
            failStep(snapshot, step,
                     "native death screen was not active");
        } else {
            completeStep(snapshot, step);
        }
        break;
    case StepKind::AssertDeathScreenInactive:
        if (snapshot.deathScreenActive) {
            failStep(snapshot, step, "native death screen remained active");
        } else {
            completeStep(snapshot, step);
        }
        break;
    case StepKind::AssertDeathAlphaAbove:
        if (snapshot.deathScreenAlpha <= step.value) {
            failStep(snapshot, step,
                     "native death-screen alpha was below the assertion");
        } else {
            completeStep(snapshot, step);
        }
        break;
    case StepKind::AssertDeathConfirmationActive:
        if (!snapshot.deathConfirmationActive) {
            failStep(snapshot, step,
                     "death confirmation is not active");
        } else {
            completeStep(snapshot, step);
        }
        break;
    case StepKind::AssertDeathConfirmationInactive:
        if (snapshot.deathConfirmationActive) {
            failStep(snapshot, step, "death confirmation remained active");
        } else {
            completeStep(snapshot, step);
        }
        break;
    case StepKind::AssertDeathConfirmationSelection:
        if (step.objectIds.empty() ||
            snapshot.deathConfirmationSelection != step.objectIds.front()) {
            failStep(snapshot, step,
                     "death confirmation selection differs from expected");
        } else {
            completeStep(snapshot, step);
        }
        break;
    case StepKind::AssertAudioPlayed:
        if (!playedAudioEvents_.contains(step.label)) {
            failStep(snapshot, step,
                     "audio event was not observed playing: " + step.label);
        } else {
            completeStep(snapshot, step);
        }
        break;
    case StepKind::AssertAudioNotPlayed:
        if (playedAudioEvents_.contains(step.label)) {
            failStep(snapshot, step,
                     "audio event was unexpectedly observed playing: " +
                         step.label);
        } else {
            completeStep(snapshot, step);
        }
        break;
    case StepKind::AssertAudioStopped:
        if (!stoppedAudioEvents_.contains(step.label)) {
            failStep(snapshot, step,
                     "audio event was not observed stopping: " + step.label);
        } else {
            completeStep(snapshot, step);
        }
        break;
    case StepKind::AssertAudioPlayCount: {
        const auto count = audioPlayCounts_.find(step.label);
        if (count == audioPlayCounts_.end() ||
            count->second < step.durationOrTimeoutMilliseconds) {
            failStep(snapshot, step,
                     "audio play count was below the assertion: " +
                         step.label);
        } else {
            completeStep(snapshot, step);
        }
        break;
    }
    case StepKind::AssertAudioStopCount: {
        const auto count = audioStopCounts_.find(step.label);
        if (count == audioStopCounts_.end() ||
            count->second < step.durationOrTimeoutMilliseconds) {
            failStep(snapshot, step,
                     "audio stop count was below the assertion: " +
                         step.label);
        } else {
            completeStep(snapshot, step);
        }
        break;
    }
    case StepKind::AssertEventNotObserved:
        if (observedEventTypes_.contains(step.label)) {
            failStep(snapshot, step,
                     "event type was unexpectedly observed: " + step.label);
        } else {
            completeStep(snapshot, step);
        }
        break;
    case StepKind::Finish:
        completeStep(snapshot, step);
        complete_ = true;
        recordEvent(snapshot.realTimeMilliseconds, "harness_complete", "ok");
        break;
    }
        return input;
    }

void AutoplayHarness::beginStep(const AutoplaySnapshot& snapshot,
                                const Step& step) {
    activeStepStartMilliseconds_ = snapshot.realTimeMilliseconds;
    activeStepPhase_ = 0;
    if (step.kind == StepKind::Attack) {
        activeAttackFarStateObserved_ = false;
    }
    recordEvent(snapshot.realTimeMilliseconds, "step_begin",
                std::to_string(activeStepIndex_) + ":" +
                    stepName(step.kind) + ":line=" +
                    std::to_string(step.sourceLine));
}

void AutoplayHarness::completeStep(const AutoplaySnapshot& snapshot,
                                   const Step& step) {
    recordEvent(snapshot.realTimeMilliseconds, "step_complete",
                std::to_string(activeStepIndex_) + ":" +
                    stepName(step.kind));
    ++activeStepIndex_;
    activeStepStartMilliseconds_.reset();
    activeStepPhase_ = 0;
}

void AutoplayHarness::notifyCinematicStarted(std::int32_t cinematicId) {
    if (std::find(observedCinematicStarts_.begin(),
                  observedCinematicStarts_.end(), cinematicId) ==
        observedCinematicStarts_.end()) {
        observedCinematicStarts_.push_back(cinematicId);
    }
}

void AutoplayHarness::failStep(const AutoplaySnapshot& snapshot,
                               const Step& step, std::string message) {
    failed_ = true;
    failureMessage_ = "step " + std::to_string(activeStepIndex_) + " (" +
                      stepName(step.kind) + ", line " +
                      std::to_string(step.sourceLine) + "): " + message;
    recordEvent(snapshot.realTimeMilliseconds, "harness_failure",
                failureMessage_);
}

game::PlayerMotionInput AutoplayHarness::steerToward(
    const AutoplaySnapshot& snapshot,
    const assets::Vector3& target) const noexcept {
    float desiredX = target.x - snapshot.playerPosition.x;
    float desiredY = target.y - snapshot.playerPosition.y;
    const float desiredLength =
        std::sqrt(desiredX * desiredX + desiredY * desiredY);
    if (desiredLength <= 0.001F) {
        return {};
    }
    desiredX /= desiredLength;
    desiredY /= desiredLength;
    float forwardX = snapshot.camera.target.x - snapshot.camera.position.x;
    float forwardY = snapshot.camera.target.y - snapshot.camera.position.y;
    const float forwardLength =
        std::sqrt(forwardX * forwardX + forwardY * forwardY);
    if (forwardLength <= 0.001F) {
        forwardX = snapshot.playerFacing.x;
        forwardY = snapshot.playerFacing.y;
    } else {
        forwardX /= forwardLength;
        forwardY /= forwardLength;
    }
    const float rightX = forwardY;
    const float rightY = -forwardX;
    return {desiredX * rightX + desiredY * rightY,
            desiredX * forwardX + desiredY * forwardY};
}

void AutoplayHarness::recordFrame(const AutoplaySnapshot& snapshot) {
    lastTimeMilliseconds_ = snapshot.realTimeMilliseconds;
    lastFrameIndex_ = snapshot.frameIndex;
    const auto transition = [this, &snapshot](std::string_view type,
                                               std::string detail) {
        recordEvent(snapshot.realTimeMilliseconds, type, detail);
    };
    if (snapshot.gameplayActive != previousGameplayActive_) {
        transition("phase", snapshot.gameplayActive ? "gameplay" : "intro");
        previousGameplayActive_ = snapshot.gameplayActive;
    }
    if (std::abs(snapshot.slowMotionDenominator -
                 previousSlowMotionDenominator_) > 0.001F) {
        transition("slow_motion",
                   std::to_string(previousSlowMotionDenominator_) + "->" +
                       std::to_string(snapshot.slowMotionDenominator));
        previousSlowMotionDenominator_ = snapshot.slowMotionDenominator;
    }
    if (snapshot.attributionEnabled != previousAttributionEnabled_) {
        transition("attribution",
                   snapshot.attributionEnabled ? "hidden->visible"
                                               : "visible->hidden");
        previousAttributionEnabled_ = snapshot.attributionEnabled;
    }
    if (snapshot.playerAnimation != previousPlayerAnimation_) {
        transition("player_animation",
                   previousPlayerAnimation_ + "->" +
                       std::string(snapshot.playerAnimation));
        previousPlayerAnimation_ = snapshot.playerAnimation;
    }
    if (snapshot.playerOnWall != previousPlayerOnWall_) {
        transition("player_on_wall",
                   previousPlayerOnWall_ ? "true->false" : "false->true");
        previousPlayerOnWall_ = snapshot.playerOnWall;
    }
    if (snapshot.playerCinematicMotionActive !=
        previousPlayerCinematicMotionActive_) {
        transition(
            "player_cinematic_motion",
            "active=" +
                std::to_string(snapshot.playerCinematicMotionActive) +
                ";elapsed_ms=" +
                std::to_string(
                    snapshot.playerCinematicMotionElapsedMilliseconds) +
                ";duration_ms=" +
                std::to_string(
                    snapshot.playerCinematicMotionDurationMilliseconds));
        previousPlayerCinematicMotionActive_ =
            snapshot.playerCinematicMotionActive;
    }
    if (snapshot.playerHealth != previousPlayerHealth_) {
        transition("player_health",
                   std::to_string(previousPlayerHealth_) + "->" +
                       std::to_string(snapshot.playerHealth));
        previousPlayerHealth_ = snapshot.playerHealth;
    }
    if (snapshot.deathConfirmationActive !=
        previousDeathConfirmationActive_) {
        transition("death_confirmation",
                   snapshot.deathConfirmationActive ? "inactive->active"
                                                    : "active->inactive");
        previousDeathConfirmationActive_ =
            snapshot.deathConfirmationActive;
    }
    if (snapshot.deathConfirmationActive &&
        snapshot.deathConfirmationSelection !=
            previousDeathConfirmationSelection_) {
        transition("death_confirmation_selection",
                   std::to_string(previousDeathConfirmationSelection_) +
                       "->" +
                       std::to_string(
                           snapshot.deathConfirmationSelection));
        previousDeathConfirmationSelection_ =
            snapshot.deathConfirmationSelection;
    }
    if (snapshot.exitMenuActive != previousExitMenuActive_) {
        transition("exit_menu",
                   snapshot.exitMenuActive ? "inactive->active"
                                           : "active->inactive");
        previousExitMenuActive_ = snapshot.exitMenuActive;
    }
    if (snapshot.exitMenuActive &&
        snapshot.exitMenuState != previousExitMenuState_) {
        transition("exit_menu_state",
                   std::to_string(previousExitMenuState_) + "->" +
                       std::to_string(snapshot.exitMenuState));
        previousExitMenuState_ = snapshot.exitMenuState;
    }
    if (snapshot.mainMenuRequested != previousMainMenuRequested_) {
        transition("main_menu_requested",
                   snapshot.mainMenuRequested ? "false->true"
                                              : "true->false");
        previousMainMenuRequested_ = snapshot.mainMenuRequested;
    }
    if (snapshot.cameraAreaId != previousCameraAreaId_) {
        transition("camera_area", std::to_string(previousCameraAreaId_) +
                                      "->" +
                                      std::to_string(snapshot.cameraAreaId));
        previousCameraAreaId_ = snapshot.cameraAreaId;
    }
    if (snapshot.lastCheckPointId != previousLastCheckPointId_) {
        transition("last_checkpoint",
                   std::to_string(previousLastCheckPointId_) + "->" +
                       std::to_string(snapshot.lastCheckPointId));
        previousLastCheckPointId_ = snapshot.lastCheckPointId;
    }
    if (snapshot.activeCinematicId != previousCinematicId_) {
        transition("active_cinematic",
                   std::to_string(previousCinematicId_) + "->" +
                       std::to_string(snapshot.activeCinematicId));
        previousCinematicId_ = snapshot.activeCinematicId;
    }
    if (snapshot.bossProgressVisible != previousBossProgressVisible_ ||
        snapshot.bossProgressClosing != previousBossProgressClosing_ ||
        snapshot.bossProgressFailed != previousBossProgressFailed_) {
        transition(
            "boss_progress",
            "visible=" + std::to_string(snapshot.bossProgressVisible) +
                ";closing=" +
                std::to_string(snapshot.bossProgressClosing) +
                ";failed=" +
                std::to_string(snapshot.bossProgressFailed) +
                ";boss=" +
                std::to_string(snapshot.bossProgressBossObjectId) +
                ";distance=" +
                std::to_string(snapshot.bossProgressDistance) +
                ";failure_distance=" +
                std::to_string(snapshot.bossProgressFailureDistance) +
                ";ratio=" +
                std::to_string(snapshot.bossProgressRatio));
        previousBossProgressVisible_ = snapshot.bossProgressVisible;
        previousBossProgressClosing_ = snapshot.bossProgressClosing;
        previousBossProgressFailed_ = snapshot.bossProgressFailed;
    }
    if (snapshot.transportState != previousTransportState_) {
        transition(
            "transport_state",
            std::to_string(static_cast<std::int32_t>(
                previousTransportState_)) +
                "->" +
                std::to_string(static_cast<std::int32_t>(
                    snapshot.transportState)) +
                ";elapsed_ms=" +
                std::to_string(snapshot.transportElapsedMilliseconds) +
                ";scale=" + std::to_string(snapshot.transportScale));
        previousTransportState_ = snapshot.transportState;
    }
    if (snapshot.levelEnded != previousLevelEnded_) {
        transition("level_ended",
                   previousLevelEnded_ ? "true->false" : "false->true");
        previousLevelEnded_ = snapshot.levelEnded;
    }
    if (snapshot.gameEnded != previousGameEnded_) {
        transition("game_ended",
                   previousGameEnded_ ? "true->false" : "false->true");
        previousGameEnded_ = snapshot.gameEnded;
    }
    const std::string visibleRooms = visibleRoomList(snapshot.visibleRooms);
    if (visibleRooms != previousVisibleRooms_) {
        transition("visible_rooms", previousVisibleRooms_ + "->" +
                                        visibleRooms);
        previousVisibleRooms_ = visibleRooms;
    }
    for (const game::RoomMotionState& room : snapshot.roomMotions) {
        const RoomTraceState current{room.active, room.targetWaypointId};
        const auto [entry, inserted] = previousRooms_.try_emplace(
            room.objectId, current);
        if (!inserted &&
            (entry->second.active != current.active ||
             entry->second.targetWaypointId != current.targetWaypointId)) {
            transition(
                "room_motion",
                "object=" + std::to_string(room.objectId) +
                    ";room=" + std::to_string(room.roomId) +
                    ";active=" + std::to_string(room.active) +
                    ";linked_waypoint=" +
                    std::to_string(room.linkedWaypointId) +
                    ";target_waypoint=" +
                    std::to_string(room.targetWaypointId) +
                    ";x=" + std::to_string(room.position.x) +
                    ";y=" + std::to_string(room.position.y) +
                    ";z=" + std::to_string(room.position.z));
            entry->second = current;
        }
    }
    for (const game::LevelEnemyState& enemy : snapshot.enemies) {
        if (enemy.asset == nullptr) {
            continue;
        }
        EnemyTraceState current{enemy.health,
                                enemy.visible,
                                enemy.aiEnabled,
                                enemy.physicsActive,
                                enemy.playerDetected,
                                enemy.behavior,
                                 enemy.activeAnimation,
                                 enemy.grounded,
                                 enemy.cinematicMotion.active,
                                 enemy.cinematicActionActive,
                                 enemy.cinematicActionObjectId,
                                 enemy.rhinoTask,
                                 enemy.rhinoPhase,
                                 enemy.rhinoSequenceCycle,
                                 enemy.robotPhantomTask,
                                 enemy.robotPhantomSequenceIndex,
                                 enemy.electroTask,
                                 enemy.electroPhase,
                                 enemy.electroSequenceIndex,
                                 enemy.electroRangeAttacksRemaining,
                                 enemy.electroDashesRemaining};
        auto [entry, inserted] = previousEnemies_.try_emplace(
            enemy.asset->objectId, current);
        if (!inserted &&
            (entry->second.health != current.health ||
             entry->second.visible != current.visible ||
             entry->second.aiEnabled != current.aiEnabled ||
             entry->second.physicsActive != current.physicsActive ||
             entry->second.playerDetected != current.playerDetected ||
             entry->second.behavior != current.behavior ||
             entry->second.animation != current.animation ||
             entry->second.grounded != current.grounded ||
              entry->second.rhinoTask != current.rhinoTask ||
              entry->second.rhinoPhase != current.rhinoPhase ||
              entry->second.rhinoSequenceCycle !=
                  current.rhinoSequenceCycle ||
              entry->second.robotPhantomTask !=
                  current.robotPhantomTask ||
              entry->second.robotPhantomSequenceIndex !=
                  current.robotPhantomSequenceIndex ||
              entry->second.electroTask != current.electroTask ||
              entry->second.electroPhase != current.electroPhase ||
              entry->second.electroSequenceIndex !=
                  current.electroSequenceIndex ||
              entry->second.electroRangeAttacksRemaining !=
                  current.electroRangeAttacksRemaining ||
              entry->second.electroDashesRemaining !=
                  current.electroDashesRemaining ||
              entry->second.cinematicActionActive !=
                  current.cinematicActionActive ||
              entry->second.cinematicActionObjectId !=
                  current.cinematicActionObjectId ||
              entry->second.cinematicMotionActive !=
                 current.cinematicMotionActive)) {
            transition("enemy_state",
                       "id=" + std::to_string(enemy.asset->objectId) +
                           ";health=" + std::to_string(current.health) +
                           ";visible=" + std::to_string(current.visible) +
                           ";ai=" + std::to_string(current.aiEnabled) +
                           ";physics_active=" +
                           std::to_string(current.physicsActive) +
                           ";detected=" +
                           std::to_string(current.playerDetected) +
                           ";behavior=" + behaviorName(current.behavior) +
                           ";animation=" + current.animation +
                           ";grounded=" +
                           std::to_string(current.grounded) +
                           ";z=" + std::to_string(enemy.position.z) +
                           ";vertical_velocity=" +
                           std::to_string(enemy.verticalVelocity) +
                           ";cinematic_motion=" +
                           std::to_string(current.cinematicMotionActive) +
                           ";cinematic_motion_ms=" +
                           std::to_string(
                               enemy.cinematicMotion.elapsedMilliseconds) +
                           ";cinematic_motion_duration_ms=" +
                           std::to_string(
                               enemy.cinematicMotion.durationMilliseconds) +
                           ";cinematic_action=" +
                           std::to_string(current.cinematicActionActive) +
                           ";cinematic_action_object=" +
                           std::to_string(current.cinematicActionObjectId) +
                           ";rhino_task=" +
                           rhinoTaskName(current.rhinoTask) +
                           ";rhino_task_ms=" +
                           std::to_string(
                               enemy.rhinoTaskElapsedMilliseconds) +
                           ";rhino_melee_remaining=" +
                           std::to_string(
                               enemy.rhinoMeleeAttacksRemaining) +
                           ";rhino_dash_x=" +
                           std::to_string(enemy.rhinoDashDirection.x) +
                            ";rhino_dash_y=" +
                            std::to_string(enemy.rhinoDashDirection.y) +
                            ";rhino_phase=" +
                            std::to_string(enemy.rhinoPhase) +
                            ";rhino_sequence_cycle=" +
                            std::to_string(enemy.rhinoSequenceCycle) +
                            ";phantom_task=" +
                            robotPhantomTaskName(
                                current.robotPhantomTask) +
                            ";phantom_task_ms=" +
                            std::to_string(
                                enemy.robotPhantomTaskElapsedMilliseconds) +
                            ";phantom_sequence_index=" +
                            std::to_string(
                                enemy.robotPhantomSequenceIndex) +
                            ";electro_task=" +
                            electroTaskName(current.electroTask) +
                            ";electro_task_ms=" +
                            std::to_string(
                                enemy.electroTaskElapsedMilliseconds) +
                            ";electro_phase=" +
                            std::to_string(enemy.electroPhase) +
                            ";electro_sequence_index=" +
                            std::to_string(enemy.electroSequenceIndex) +
                            ";electro_range_remaining=" +
                            std::to_string(
                                enemy.electroRangeAttacksRemaining) +
                            ";electro_range_wait_ms=" +
                            std::to_string(
                                enemy.electroRangeWaitMilliseconds) +
                            ";electro_dash_remaining=" +
                            std::to_string(enemy.electroDashesRemaining) +
                            ";electro_dash_target_x=" +
                            std::to_string(enemy.electroDashTarget.x) +
                            ";electro_dash_target_y=" +
                            std::to_string(enemy.electroDashTarget.y) +
                            ";electro_dash_target_z=" +
                            std::to_string(enemy.electroDashTarget.z));
            entry->second = std::move(current);
        }
    }
    for (const game::LevelObjectState& object : snapshot.objects) {
        if (object.asset == nullptr) {
            continue;
        }
        ObjectTraceState current{object.health,
                                 object.visible,
                                 object.collisionEnabled,
                                 object.comicCollected,
                                 object.destructionPhase,
                                 object.activeAnimation,
                                 object.electricState,
                                 object.areaDamageState,
                                 object.platformMotionState,
                                 object.platformMotionActive,
                                 object.trainActive,
                                 object.trainCut,
                                 object.trainTargetWaypointId,
                                 object.cinematicMotion.active};
        auto [entry, inserted] = previousObjects_.try_emplace(
            object.asset->objectId, current);
        if (!inserted &&
            (entry->second.health != current.health ||
             entry->second.visible != current.visible ||
             entry->second.collisionEnabled != current.collisionEnabled ||
              entry->second.comicCollected != current.comicCollected ||
              entry->second.destructionPhase != current.destructionPhase ||
              entry->second.animation != current.animation ||
              entry->second.electricState != current.electricState ||
              entry->second.areaDamageState != current.areaDamageState ||
              entry->second.platformMotionState !=
                  current.platformMotionState ||
              entry->second.platformMotionActive !=
                  current.platformMotionActive ||
              entry->second.trainActive != current.trainActive ||
              entry->second.trainCut != current.trainCut ||
              entry->second.trainTargetWaypointId !=
                  current.trainTargetWaypointId ||
              entry->second.cinematicMotionActive !=
                  current.cinematicMotionActive)) {
            transition(
                "object_state",
                "id=" + std::to_string(object.asset->objectId) +
                    ";health=" + std::to_string(current.health) +
                    ";visible=" + std::to_string(current.visible) +
                    ";collision=" +
                    std::to_string(current.collisionEnabled) +
                    ";comic_collected=" +
                    std::to_string(current.comicCollected) +
                    ";destruction_phase=" +
                     std::to_string(static_cast<std::int32_t>(
                         current.destructionPhase)) +
                    ";animation=" + current.animation +
                    ";electric_state=" +
                    std::to_string(static_cast<std::int32_t>(
                        current.electricState)) +
                    ";electric_state_ms=" +
                    std::to_string(object.electricStateElapsedMilliseconds) +
                    ";electric_switch=" +
                    std::to_string(object.electricSwitchActive) +
                    ";area_damage_state=" +
                    std::to_string(current.areaDamageState) +
                    ";area_damage_state_ms=" +
                    std::to_string(object.areaDamageStateMilliseconds) +
                    ";area_damage_wait_ms=" +
                    std::to_string(object.areaDamageWaitMilliseconds) +
                    ";area_damage_contact_cooldown_ms=" +
                    std::to_string(
                        object.areaDamageContactCooldownMilliseconds) +
                    ";area_damage_player_hit=" +
                    std::to_string(object.areaDamagePlayerHit) +
                    ";platform_motion_state=" +
                    std::to_string(static_cast<std::int32_t>(
                        current.platformMotionState)) +
                    ";platform_motion_active=" +
                    std::to_string(current.platformMotionActive) +
                    ";platform_target=" +
                    std::to_string(object.platformTargetWaypointId) +
                    ";train_active=" +
                    std::to_string(current.trainActive) +
                    ";train_cut=" + std::to_string(current.trainCut) +
                    ";train_speed=" +
                    std::to_string(
                        object.trainCurrentSpeedCentimetersPerMillisecond) +
                    ";train_target=" +
                    std::to_string(current.trainTargetWaypointId) +
                    ";cinematic_motion=" +
                    std::to_string(current.cinematicMotionActive) +
                    ";cinematic_motion_ms=" +
                    std::to_string(
                        object.cinematicMotion.elapsedMilliseconds) +
                    ";cinematic_motion_duration_ms=" +
                    std::to_string(
                        object.cinematicMotion.durationMilliseconds));
            entry->second = std::move(current);
        }
    }
    for (const game::LevelBonusState& bonus : snapshot.bonuses) {
        if (bonus.asset == nullptr) {
            continue;
        }
        BonusTraceState current{bonus.visible, bonus.orbActive,
                                bonus.progress};
        auto [entry, inserted] = previousBonuses_.try_emplace(
            bonus.asset->objectId, current);
        if (!inserted &&
            (entry->second.visible != current.visible ||
             entry->second.orbActive != current.orbActive)) {
            transition(
                "bonus_state",
                "id=" + std::to_string(bonus.asset->objectId) +
                    ";type=" + std::to_string(static_cast<std::int32_t>(
                        bonus.asset->type)) +
                    ";visible=" + std::to_string(current.visible) +
                    ";orb_active=" + std::to_string(current.orbActive) +
                    ";progress=" + std::to_string(current.progress));
        }
        entry->second = current;
    }
    for (const game::LevelHostageState& hostage : snapshot.hostages) {
        if (hostage.asset == nullptr) {
            continue;
        }
        HostageTraceState current{hostage.phase, hostage.completedActions,
                                  hostage.promptVisible};
        auto [entry, inserted] = previousHostages_.try_emplace(
            hostage.asset->objectId, current);
        if (!inserted &&
            (entry->second.phase != current.phase ||
             entry->second.completedActions != current.completedActions ||
             entry->second.promptVisible != current.promptVisible)) {
            transition(
                "hostage_state",
                "id=" + std::to_string(hostage.asset->objectId) +
                    ";phase=" + std::to_string(static_cast<std::int32_t>(
                        current.phase)) +
                    ";phase_ms=" +
                    std::to_string(hostage.phaseElapsedMilliseconds) +
                    ";prompt=" + std::to_string(current.promptVisible) +
                    ";qte_ms=" +
                    std::to_string(hostage.quickTimeElapsedMilliseconds) +
                    ";actions=" +
                    std::to_string(current.completedActions) + "/" +
                    std::to_string(hostage.requiredActions));
        }
        entry->second = current;
    }
    for (const game::LevelDropObjectState& drop : snapshot.drops) {
        if (drop.asset == nullptr) {
            continue;
        }
        DropTraceState current{drop.phase, drop.visible,
                               drop.physicsEnabled, drop.hitPlayer};
        auto [entry, inserted] = previousDrops_.try_emplace(
            drop.asset->objectId, current);
        if (!inserted &&
            (entry->second.phase != current.phase ||
             entry->second.visible != current.visible ||
             entry->second.physicsEnabled != current.physicsEnabled ||
             entry->second.hitPlayer != current.hitPlayer)) {
            transition(
                "drop_state",
                "id=" + std::to_string(drop.asset->objectId) +
                    ";owner=" + std::to_string(drop.asset->ownerAreaId) +
                    ";phase=" + std::to_string(static_cast<std::int32_t>(
                        current.phase)) +
                    ";visible=" + std::to_string(current.visible) +
                    ";physics=" +
                    std::to_string(current.physicsEnabled) +
                    ";hit_player=" + std::to_string(current.hitPlayer) +
                    ";z=" + std::to_string(drop.position.z) +
                    ";velocity=" +
                    std::to_string(drop.downwardVelocity));
        }
        entry->second = current;
    }

    if (snapshot.realTimeMilliseconds < nextSampleMilliseconds_) {
        return;
    }
    nextSampleMilliseconds_ = snapshot.realTimeMilliseconds +
                              sampleIntervalMilliseconds_;
    std::string activeCinematics;
    for (const std::int32_t cinematicId : snapshot.activeCinematicIds) {
        if (!activeCinematics.empty()) {
            activeCinematics += '|';
        }
        activeCinematics += std::to_string(cinematicId);
    }
    frameLog_ << snapshot.frameIndex << ',' << snapshot.realTimeMilliseconds
              << ',' << snapshot.gameTimeMilliseconds << ','
              << snapshot.slowMotionDenominator << ','
              << (snapshot.gameplayActive ? "gameplay" : "intro") << ','
              << snapshot.controlsEnabled << ','
              << snapshot.attributionEnabled << ','
              << snapshot.playerPosition.x
              << ',' << snapshot.playerPosition.y << ','
              << snapshot.playerPosition.z << ','
              << snapshot.playerRenderPosition.x << ','
              << snapshot.playerRenderPosition.y << ','
              << snapshot.playerRenderPosition.z << ','
              << snapshot.playerAttackRootTranslation.x << ','
              << snapshot.playerAttackRootTranslation.y << ','
              << snapshot.playerAttackRootTranslation.z << ','
              << snapshot.playerFacing.x
              << ',' << snapshot.playerFacing.y << ','
              << snapshot.playerFacing.z << ',' << snapshot.playerHealth << ','
              << snapshot.playerSkillPoints << ','
              << snapshot.playerComboScore << ','
              << csv(snapshot.playerAnimation) << ','
              << snapshot.playerAnimationTimeMilliseconds << ','
              << snapshot.playerStateId << ','
              << csv(snapshot.playerStateName) << ','
              << snapshot.playerPunchTransitionReady << ','
              << snapshot.playerPunchAttackTransitionReady << ','
              << snapshot.playerJumpAttackTransitionReady << ','
              << snapshot.playerJumpReleaseAttackTransitionReady << ','
              << snapshot.playerWebAttackTransitionReady << ','
              << snapshot.playerWebHeldAttackTransitionReady << ','
              << snapshot.playerOnWall << ','
              << snapshot.playerCinematicMotionActive << ','
              << snapshot.playerCinematicMotionElapsedMilliseconds << ','
              << snapshot.playerCinematicMotionDurationMilliseconds << ','
               << snapshot.cameraAreaId << ',' << snapshot.lastCheckPointId
               << ',' << snapshot.activeCinematicId
              << ',' << csv(activeCinematics) << ','
              << snapshot.quickTimeEventActive << ','
              << snapshot.hostageQuickTimeEventActive << ','
              << snapshot.tutorialVisible << ','
              << snapshot.deathScreenActive << ','
              << snapshot.deathScreenAlpha << ','
              << snapshot.deathConfirmationActive << ','
              << snapshot.deathConfirmationSelection << ','
              << snapshot.exitMenuActive << ','
              << snapshot.exitMenuState << ','
              << snapshot.mainMenuRequested << ','
              << snapshot.restoreActive << ',' << snapshot.restoreAlpha << ','
              << csv(visibleRooms) << ',' << lastMotionInput_.right << ','
              << lastMotionInput_.forward << ',' << snapshot.camera.position.x
              << ',' << snapshot.camera.position.y << ','
              << snapshot.camera.position.z << ',' << snapshot.camera.target.x
              << ',' << snapshot.camera.target.y << ','
              << snapshot.camera.target.z << ','
              << snapshot.rhinoQuickTimeActionActive << ','
              << snapshot.rhinoQuickTimeActionState << ','
              << snapshot.rhinoQuickTimeStateElapsedMilliseconds << ','
              << snapshot.rhinoQuickTimeButtonElapsedMilliseconds << ','
              << snapshot.rhinoQuickTimeDurationMilliseconds << ','
              << snapshot.rhinoQuickTimeCompletedActions << ','
              << snapshot.rhinoQuickTimeRequiredActions << ','
              << snapshot.rhinoQuickTimeProgress << ','
              << snapshot.bossProgressVisible << ','
              << snapshot.bossProgressClosing << ','
              << snapshot.bossProgressFailed << ','
              << snapshot.bossProgressBossObjectId << ','
              << snapshot.bossProgressDistance << ','
              << snapshot.bossProgressFailureDistance << ','
              << snapshot.bossProgressRatio << ','
              << static_cast<std::int32_t>(snapshot.transportState) << ','
              << snapshot.transportElapsedMilliseconds << ','
              << snapshot.transportScale << ',' << snapshot.levelEnded
              << ',' << snapshot.gameEnded << ','
              << snapshot.cinematicLetterboxVisible << ','
              << static_cast<int>(snapshot.wallWebPhase) << ','
              << snapshot.wallWebTargetObjectId << ',' << snapshot.wallWebAngle << ','
              << snapshot.wallWebCompletedActions << ',' << snapshot.wallWebLineActive << '\n';
    for (const game::RoomMotionState& room : snapshot.roomMotions) {
        roomLog_ << snapshot.frameIndex << ','
                 << snapshot.realTimeMilliseconds << ',' << room.objectId
                 << ',' << room.roomId << ',' << room.movingRoom << ','
                 << room.active << ',' << room.linkedWaypointId << ','
                 << room.targetWaypointId << ',' << room.position.x << ','
                 << room.position.y << ',' << room.position.z << ','
                 << room.velocity.x << ',' << room.velocity.y << ','
                 << room.velocity.z << '\n';
    }
    for (const game::LevelEnemyState& enemy : snapshot.enemies) {
        if (enemy.asset == nullptr) {
            continue;
        }
        enemyLog_ << snapshot.frameIndex << ','
                  << snapshot.realTimeMilliseconds << ','
                  << enemy.asset->objectId << ',' << enemy.asset->enemyTypeId
                  << ',' << enemy.position.x << ',' << enemy.position.y << ','
                  << enemy.position.z << ',' << enemy.health << ','
                  << enemy.visible << ',' << enemy.aiEnabled << ','
                  << enemy.physicsActive << ','
                  << enemy.playerDetected << ',' << behaviorName(enemy.behavior)
                  << ',' << csv(enemy.activeAnimation) << ','
                  << enemy.animationTimeMilliseconds << ','
                  << enemy.animationSpeed << ',' << enemy.animationLoops << ','
                  << enemy.animationReversed << ',' << enemy.collisionRadius
                  << ',' << enemy.collisionHeight << ','
                  << enemy.verticalVelocity << ',' << enemy.hurtStateId << ','
                  << enemy.lastPlayerHitType << ',' << enemy.hurtVelocity.x
                  << ',' << enemy.hurtVelocity.y << ','
                  << enemy.hurtVelocity.z << ',' << enemy.grounded << ','
                  << enemy.anchoredWithoutSupport << ','
                  << enemy.cinematicMotion.active << ','
                  << enemy.cinematicMotion.elapsedMilliseconds << ','
                  << enemy.cinematicMotion.durationMilliseconds << ','
                  << enemy.cinematicActionActive << ','
                  << enemy.cinematicActionObjectId << ','
                  << enemy.meleeAttackActive << ','
                  << enemy.meleeAttackRegistered << ','
                  << enemy.meleeAttackCooldownMilliseconds << ','
                  << enemy.rangeAttackCooldownMilliseconds << ','
                  << static_cast<int>(enemy.sandmanTask) << ','
                  << enemy.sandmanJumpElapsedMilliseconds << ','
                  << enemy.sandmanJumpDurationMilliseconds << ','
                  << rhinoTaskName(enemy.rhinoTask) << ','
                  << enemy.rhinoTaskElapsedMilliseconds << ','
                  << enemy.rhinoMeleeAttacksRemaining << ','
                  << enemy.rhinoDashDirection.x << ','
                  << enemy.rhinoDashDirection.y << ',' << enemy.rhinoPhase
                  << ',' << enemy.rhinoSequenceCycle << ','
                  << robotPhantomTaskName(enemy.robotPhantomTask) << ','
                  << enemy.robotPhantomTaskElapsedMilliseconds << ','
                  << enemy.robotPhantomSequenceIndex << ','
                  << electroTaskName(enemy.electroTask) << ','
                  << enemy.electroTaskElapsedMilliseconds << ','
                  << enemy.electroPhase << ','
                  << enemy.electroSequenceIndex << ','
                  << enemy.electroRangeAttacksRemaining << ','
                  << enemy.electroRangeWaitMilliseconds << ','
                  << enemy.electroDashesRemaining << ','
                  << enemy.electroDashTarget.x << ','
                  << enemy.electroDashTarget.y << ','
                  << enemy.electroDashTarget.z << ','
                  << enemy.onWall << ',' << enemy.wallAttached << ','
                  << enemy.wallNormal.x << ',' << enemy.wallNormal.y << ','
                  << enemy.wallNormal.z << ',' << enemy.wallBehaviorState << ','
                  << enemy.wallMoveTarget.x << ',' << enemy.wallMoveTarget.y
                  << ',' << enemy.wallMoveTarget.z << ',' << enemy.wallWebCaptured << '\n';
    }
    for (const game::EnemyMolotovState& molotov : snapshot.molotovs) {
        projectileLog_ << snapshot.frameIndex << ','
                       << snapshot.realTimeMilliseconds << ",molotov,"
                       << molotov.sourceObjectId << ',' << molotov.roomId
                       << ',' << molotov.position.x << ','
                       << molotov.position.y << ',' << molotov.position.z
                       << ',' << molotov.velocity.x << ','
                       << molotov.velocity.y << ',' << molotov.velocity.z
                       << ',' << static_cast<std::int32_t>(molotov.phase)
                       << ',' << molotov.phaseElapsedMilliseconds << ','
                       << molotov.damage << ',' << molotov.active << ",1\n";
    }
    for (const game::EnemyBoomerangState& boomerang :
         snapshot.boomerangs) {
        projectileLog_ << snapshot.frameIndex << ','
                       << snapshot.realTimeMilliseconds << ",boomerang,"
                       << boomerang.sourceObjectId << ','
                       << boomerang.roomId << ',' << boomerang.position.x
                       << ',' << boomerang.position.y << ','
                       << boomerang.position.z << ',' << boomerang.velocity.x
                       << ',' << boomerang.velocity.y << ','
                       << boomerang.velocity.z << ','
                       << static_cast<std::int32_t>(boomerang.phase) << ','
                       << boomerang.phaseElapsedMilliseconds << ','
                       << boomerang.damage << ',' << boomerang.active
                       << ",1\n";
    }
    for (const game::EnemyThunderclapState& thunderclap :
         snapshot.thunderclaps) {
        projectileLog_ << snapshot.frameIndex << ','
                       << snapshot.realTimeMilliseconds << ",thunderclap,"
                       << thunderclap.sourceObjectId << ','
                       << thunderclap.roomId << ',' << thunderclap.position.x
                       << ',' << thunderclap.position.y << ','
                       << thunderclap.position.z << ','
                       << thunderclap.velocity.x << ','
                       << thunderclap.velocity.y << ','
                       << thunderclap.velocity.z << ','
                       << static_cast<std::int32_t>(thunderclap.phase) << ','
                       << thunderclap.phaseElapsedMilliseconds << ','
                       << thunderclap.damage << ',' << thunderclap.active
                       << ",1\n";
    }
    for (const game::EnemyElectricPostState& post : snapshot.electricPosts) {
        projectileLog_ << snapshot.frameIndex << ','
                       << snapshot.realTimeMilliseconds << ",electric_post,"
                       << post.sourceObjectId << ',' << post.roomId << ','
                       << post.position.x << ',' << post.position.y << ','
                       << post.position.z << ',' << post.facing.x << ','
                       << post.facing.y << ',' << post.facing.z << ",2,"
                       << post.animationTimeMilliseconds << ',' << post.damage
                       << ',' << post.active << ",1\n";
    }
    for (const game::EnemyElectroBurstState& burst : snapshot.electroBursts) {
        projectileLog_ << snapshot.frameIndex << ','
                       << snapshot.realTimeMilliseconds << ",electro_burst,"
                       << burst.sourceObjectId << ',' << burst.roomId << ','
                       << burst.position.x << ',' << burst.position.y << ','
                       << burst.position.z << ",0,0,0,1,"
                       << burst.elapsedMilliseconds << ",0," << burst.active
                       << ',' << burst.scale << '\n';
    }
    for (const game::LevelObjectState& object : snapshot.objects) {
        if (object.asset == nullptr) {
            continue;
        }
        objectLog_ << snapshot.frameIndex << ','
                   << snapshot.realTimeMilliseconds << ','
                   << object.asset->objectId << ','
                   << static_cast<std::int32_t>(object.asset->kind) << ','
                   << object.position.x << ',' << object.position.y << ','
                   << object.position.z << ',' << object.health << ','
                   << object.visible << ',' << object.physicsEnabled << ','
                   << object.collisionEnabled << ','
                   << static_cast<std::int32_t>(object.destructionPhase) << ','
                   << object.comicCollected << ','
                   << csv(object.activeAnimation) << ','
                   << object.animationTimeMilliseconds << ','
                   << object.animationLoops << ','
                   << static_cast<std::int32_t>(object.electricState) << ','
                   << object.electricStateElapsedMilliseconds << ','
                   << object.electricSwitchActive << ','
                   << static_cast<std::int32_t>(object.platformMotionState)
                   << ',' << object.platformMotionActive << ','
                   << object.platformMotionElapsedMilliseconds << ','
                   << object.platformTargetWaypointId << ','
                   << object.trainActive << ',' << object.trainCut << ','
                   << object.trainCurrentSpeedCentimetersPerMillisecond << ','
                   << object.trainTargetWaypointId << ','
                   << object.trainPreviousObjectId << ','
                   << object.trainNextObjectId << ','
                   << object.trainDirection.x << ','
                   << object.trainDirection.y << ','
                   << object.trainDirection.z << ','
                   << object.trainRotation.x << ','
                   << object.trainRotation.y << ','
                   << object.trainRotation.z << ','
                   << object.trainRotation.w << ','
                   << object.trainVelocityCentimetersPerSecond.x << ','
                   << object.trainVelocityCentimetersPerSecond.y << ','
                   << object.trainVelocityCentimetersPerSecond.z << ','
                   << object.cinematicMotion.active << ','
                   << object.cinematicMotion.elapsedMilliseconds << ','
                   << object.cinematicMotion.durationMilliseconds << ','
                   << object.bridgeState << ',' << object.bridgeStateSeconds << ','
                   << object.bridgeVelocity.z << ','
                   << object.slideCarState << ','
                   << object.slideCarStateSeconds << ','
                   << object.slideCarBridgeObjectId << ','
                   << object.slideCarVelocity.x << ','
                   << object.slideCarVelocity.y << ','
                   << object.slideCarVelocity.z << ','
                   << object.slideCarRotation.x << ','
                   << object.slideCarRotation.y << ','
                   << object.slideCarRotation.z << ','
                   << object.slideCarRotation.w << ','
                   << object.areaDamageState << ','
                   << object.areaDamageStateMilliseconds << ','
                   << object.areaDamageWaitMilliseconds << ','
                   << object.areaDamageContactCooldownMilliseconds << ','
                   << object.areaDamagePlayerHit << '\n';
    }
    for (const game::LevelBonusState& bonus : snapshot.bonuses) {
        if (bonus.asset == nullptr) {
            continue;
        }
        bonusLog_ << snapshot.frameIndex << ','
                  << snapshot.realTimeMilliseconds << ','
                  << bonus.asset->objectId << ','
                  << static_cast<std::int32_t>(bonus.asset->type) << ','
                  << bonus.asset->roomId << ',' << bonus.asset->position.x
                  << ',' << bonus.asset->position.y << ','
                  << bonus.asset->position.z << ',' << bonus.visible << ','
                  << bonus.orbActive << ',' << bonus.progress << '\n';
    }
    for (const game::LevelHostageState& hostage : snapshot.hostages) {
        if (hostage.asset == nullptr) {
            continue;
        }
        hostageLog_ << snapshot.frameIndex << ','
                    << snapshot.realTimeMilliseconds << ','
                    << hostage.asset->objectId << ','
                    << hostage.asset->roomId << ','
                    << static_cast<std::int32_t>(hostage.phase) << ','
                    << hostage.phaseElapsedMilliseconds << ','
                    << hostage.promptVisible << ','
                    << hostage.quickTimeElapsedMilliseconds << ','
                    << hostage.quickTimeDurationMilliseconds << ','
                    << hostage.completedActions << ','
                    << hostage.requiredActions << '\n';
    }
    for (const game::LevelDropObjectState& drop : snapshot.drops) {
        if (drop.asset == nullptr) {
            continue;
        }
        dropLog_ << snapshot.frameIndex << ','
                 << snapshot.realTimeMilliseconds << ','
                 << drop.asset->objectId << ',' << drop.asset->ownerAreaId
                 << ',' << drop.asset->roomId << ',' << drop.position.x << ','
                 << drop.position.y << ',' << drop.position.z << ','
                 << static_cast<std::int32_t>(drop.phase) << ','
                 << drop.visible << ',' << drop.physicsEnabled << ','
                 << drop.hitPlayer << ',' << drop.downwardVelocity << ','
                 << drop.delayRemainingMilliseconds << '\n';
    }
    frameLog_.flush();
    roomLog_.flush();
    enemyLog_.flush();
    objectLog_.flush();
    bonusLog_.flush();
    hostageLog_.flush();
    dropLog_.flush();
}

void AutoplayHarness::recordEvent(std::uint64_t timeMilliseconds,
                                  std::string_view type,
                                  std::string_view detail) {
    observedEventTypes_.emplace(type);
    if (!eventLog_) {
        return;
    }
    eventLog_ << timeMilliseconds << ',' << lastFrameIndex_ << ',' << csv(type)
              << ',' << csv(detail) << '\n';
    eventLog_.flush();
}

void AutoplayHarness::recordCommand(
    std::uint64_t timeMilliseconds, std::int32_t cinematicId,
    std::int32_t threadObjectId,
    const game::CinematicCommand& command) {
    std::string detail =
        "cinematic=" + std::to_string(cinematicId) + ";thread=" +
        std::to_string(threadObjectId) + ";name=" + command.name +
        ";timestamp=" +
        std::to_string(command.timestampMilliseconds);
    for (const game::CinematicAttribute& attribute : command.attributes) {
        detail += ";" + attribute.name + "=" + attribute.value;
    }
    recordEvent(timeMilliseconds, "cinematic_command", detail);
}

void AutoplayHarness::recordCollisionAssets(
    std::span<const game::LevelRoomAsset> rooms) {
    if (!geometryAssetLog_ || !collisionAssetLog_ ||
        !collisionTriangleLog_ || !navigationTriangleLog_) {
        return;
    }
    for (std::size_t roomIndex = 0; roomIndex < rooms.size(); ++roomIndex) {
        const game::LevelRoomAsset& room = rooms[roomIndex];
        for (const assets::ColladaGeometry& geometry :
             room.geometry.sceneGeometries()) {
            for (std::size_t bufferIndex = 0;
                 bufferIndex < geometry.meshBuffers.size(); ++bufferIndex) {
                const assets::ColladaMeshBuffer& buffer =
                    geometry.meshBuffers[bufferIndex];
                const assets::ColladaMaterial* material =
                    room.geometry.findMaterial(buffer.materialName);
                const std::string lightmapImage =
                    material != nullptr && material->lightmapImageIndex &&
                            *material->lightmapImageIndex <
                                room.geometry.images().size()
                        ? room.geometry.images()[*material->lightmapImageIndex]
                              .sourcePath
                        : std::string{};
                geometryAssetLog_
                    << (roomIndex + 1) << ',' << csv(geometry.name) << ','
                    << bufferIndex << ',' << csv(buffer.materialName) << ','
                    << geometry.vertices.size() << ',' << buffer.indices.size()
                    << ',' << buffer.usesSecondaryTextureCoordinates << ','
                    << csv(lightmapImage) << ',' << geometry.bounds.minimum.x
                    << ',' << geometry.bounds.minimum.y << ','
                    << geometry.bounds.minimum.z << ','
                    << geometry.bounds.maximum.x << ','
                    << geometry.bounds.maximum.y << ','
                    << geometry.bounds.maximum.z << '\n';
            }
        }
        for (const assets::ColladaGeometry& geometry :
             room.collision.sceneGeometries()) {
            std::string_view surfaceClass = "collision";
            if (geometry.name.starts_with("wall")) {
                surfaceClass = "climbable_wall";
            } else if (geometry.name.starts_with("jump_wall")) {
                surfaceClass = "jump_wall";
            } else if (geometry.name.starts_with("edge_wall")) {
                surfaceClass = "edge_wall";
            }
            collisionAssetLog_
                << (roomIndex + 1) << ',' << csv(geometry.name) << ','
                << surfaceClass << ',' << geometry.bounds.minimum.x << ','
                << geometry.bounds.minimum.y << ','
                << geometry.bounds.minimum.z << ','
                << geometry.bounds.maximum.x << ','
                << geometry.bounds.maximum.y << ','
                << geometry.bounds.maximum.z << '\n';
            std::size_t triangleIndex = 0;
            const auto recordTriangle =
                [&](std::uint16_t firstIndex,
                    std::uint16_t secondIndex,
                    std::uint16_t thirdIndex) {
                    if (firstIndex >= geometry.vertices.size() ||
                        secondIndex >= geometry.vertices.size() ||
                        thirdIndex >= geometry.vertices.size()) {
                        return;
                    }
                    const assets::Vector3& first =
                        geometry.vertices[firstIndex].position;
                    const assets::Vector3& second =
                        geometry.vertices[secondIndex].position;
                    const assets::Vector3& third =
                        geometry.vertices[thirdIndex].position;
                    const assets::Vector3 firstEdge{
                        second.x - first.x, second.y - first.y,
                        second.z - first.z};
                    const assets::Vector3 secondEdge{
                        third.x - first.x, third.y - first.y,
                        third.z - first.z};
                    assets::Vector3 normal{
                        firstEdge.y * secondEdge.z -
                            firstEdge.z * secondEdge.y,
                        firstEdge.z * secondEdge.x -
                            firstEdge.x * secondEdge.z,
                        firstEdge.x * secondEdge.y -
                            firstEdge.y * secondEdge.x};
                    const float normalLength =
                        std::sqrt(normal.x * normal.x + normal.y * normal.y +
                                  normal.z * normal.z);
                    if (normalLength <=
                        std::numeric_limits<float>::epsilon()) {
                        return;
                    }
                    normal.x /= normalLength;
                    normal.y /= normalLength;
                    normal.z /= normalLength;
                    const bool vertical =
                        normal.z <=
                        game::LevelCollisionConstants::MinimumGroundNormalZ;
                    std::uint32_t physicsFlags =
                        game::LevelPhysicsFlags::Wall;
                    if (surfaceClass == "jump_wall") {
                        physicsFlags = game::LevelPhysicsFlags::JumpWall;
                    } else if (surfaceClass == "edge_wall") {
                        physicsFlags =
                            game::LevelPhysicsFlags::ClimbableEdge;
                    } else if (surfaceClass == "climbable_wall" &&
                               vertical) {
                        physicsFlags =
                            game::LevelPhysicsFlags::ClimbableWall;
                    } else {
                        physicsFlags = vertical
                                           ? game::LevelPhysicsFlags::Wall
                                           : game::LevelPhysicsFlags::Ground;
                    }
                    if (geometry.name.starts_with("double")) {
                        physicsFlags |= game::LevelPhysicsFlags::DoubleSided;
                    }
                    collisionTriangleLog_
                        << (roomIndex + 1) << ',' << csv(geometry.name) << ','
                        << triangleIndex++ << ',' << physicsFlags << ','
                        << first.x << ',' << first.y << ',' << first.z << ','
                        << second.x << ',' << second.y << ',' << second.z
                        << ',' << third.x << ',' << third.y << ',' << third.z
                        << ',' << normal.x << ',' << normal.y << ','
                        << normal.z << '\n';
                };
            for (const assets::ColladaMeshBuffer& buffer :
                 geometry.meshBuffers) {
                if (buffer.primitive ==
                    assets::ColladaPrimitive::Triangles) {
                    for (std::size_t index = 0;
                         index + 2 < buffer.indices.size(); index += 3) {
                        recordTriangle(buffer.indices[index],
                                       buffer.indices[index + 1],
                                       buffer.indices[index + 2]);
                    }
                } else if (buffer.primitive ==
                           assets::ColladaPrimitive::TriangleStrip) {
                    for (std::size_t index = 2;
                         index < buffer.indices.size(); ++index) {
                        const bool odd = (index & 1U) != 0;
                        recordTriangle(
                            buffer.indices[index - (odd ? 0 : 2)],
                            buffer.indices[index - 1],
                            buffer.indices[index - (odd ? 2 : 0)]);
                    }
                }
            }
        }
        for (const assets::ColladaGeometry& geometry :
             room.navigationMesh.sceneGeometries()) {
            std::size_t triangleIndex = 0;
            const auto recordNavigationTriangle =
                [&](std::uint16_t firstIndex,
                    std::uint16_t secondIndex,
                    std::uint16_t thirdIndex) {
                    if (firstIndex >= geometry.vertices.size() ||
                        secondIndex >= geometry.vertices.size() ||
                        thirdIndex >= geometry.vertices.size()) {
                        return;
                    }
                    const assets::Vector3& first =
                        geometry.vertices[firstIndex].position;
                    const assets::Vector3& second =
                        geometry.vertices[secondIndex].position;
                    const assets::Vector3& third =
                        geometry.vertices[thirdIndex].position;
                    navigationTriangleLog_
                        << (roomIndex + 1) << ',' << csv(geometry.name) << ','
                        << triangleIndex++ << ',' << first.x << ','
                        << first.y << ',' << first.z << ',' << second.x << ','
                        << second.y << ',' << second.z << ',' << third.x << ','
                        << third.y << ',' << third.z << '\n';
                };
            for (const assets::ColladaMeshBuffer& buffer :
                 geometry.meshBuffers) {
                if (buffer.primitive ==
                    assets::ColladaPrimitive::Triangles) {
                    for (std::size_t index = 0;
                         index + 2 < buffer.indices.size(); index += 3) {
                        recordNavigationTriangle(
                            buffer.indices[index], buffer.indices[index + 1],
                            buffer.indices[index + 2]);
                    }
                } else if (buffer.primitive ==
                           assets::ColladaPrimitive::TriangleStrip) {
                    for (std::size_t index = 2;
                         index < buffer.indices.size(); ++index) {
                        const bool odd = (index & 1U) != 0;
                        recordNavigationTriangle(
                            buffer.indices[index - (odd ? 0 : 2)],
                            buffer.indices[index - 1],
                            buffer.indices[index - (odd ? 2 : 0)]);
                    }
                }
            }
        }
    }
    geometryAssetLog_.flush();
    collisionAssetLog_.flush();
    collisionTriangleLog_.flush();
    navigationTriangleLog_.flush();
}

void AutoplayHarness::recordMaterialAssets(
    const game::LevelOneBootstrap& level) {
    if (!sceneNodeAssetLog_ || !objectAssetLog_ || !triggerAssetLog_ || !waypointAssetLog_ ||
        !webGrabPointAssetLog_ || !slideAssetLog_ || !checkPointAssetLog_ ||
        !cameraAreaAssetLog_ ||
        !materialAssetLog_ ||
        !textureAssetLog_ ||
        !skyTriangleAssetLog_ || !colladaNodeAssetLog_) {
        return;
    }
    const auto recordScene = [&](std::string_view source,
                                 std::int32_t roomId,
                                 const assets::IrrScene& scene) {
        for (const assets::IrrSceneNode& node : scene.nodes()) {
            std::vector<std::string_view> attributeNames;
            attributeNames.reserve(node.userAttributes.size());
            for (const auto& [name, value] : node.userAttributes) {
                (void)value;
                attributeNames.push_back(name);
            }
            std::sort(attributeNames.begin(), attributeNames.end());
            std::string attributes;
            for (const std::string_view name : attributeNames) {
                if (!attributes.empty()) {
                    attributes += '|';
                }
                attributes += name;
                attributes += '=';
                attributes += node.userAttributes.at(std::string(name));
            }
            sceneNodeAssetLog_
                << csv(source) << ',' << roomId << ',' << node.id << ','
                << node.parentId << ',' << csv(node.sceneType) << ','
                << csv(node.name) << ',' << csv(node.gameType) << ','
                << node.visible << ',' << node.hasCollision << ','
                << csv(node.meshFile) << ',' << csv(node.animationFile) << ','
                << csv(node.initialAnimation) << ',' << node.position.x << ','
                << node.position.y << ',' << node.position.z << ','
                << node.absoluteTransform[12] << ','
                << node.absoluteTransform[13] << ','
                << node.absoluteTransform[14] << ',' << node.rotation.x << ','
                << node.rotation.y << ',' << node.rotation.z << ','
                << node.rotation.w << ',' << node.scale.x << ','
                << node.scale.y << ',' << node.scale.z << ','
                << csv(attributes) << '\n';
        }
    };
    recordScene("main", 0, level.mainScene());
    for (std::size_t roomIndex = 0; roomIndex < level.rooms().size(); ++roomIndex) {
        const game::LevelRoomAsset& room = level.rooms()[roomIndex];
        recordScene(room.sceneFile, static_cast<std::int32_t>(roomIndex + 1),
                    room.scene);
    }
    sceneNodeAssetLog_.flush();
    for (const game::LevelObjectAsset& object : level.objects()) {
        objectAssetLog_
            << object.objectId << ',' << csv(object.name) << ','
            << csv(object.gameType) << ','
            << static_cast<std::uint32_t>(object.kind) << ',' << object.roomId
            << ',' << object.visible << ',' << object.hasCollision << ','
            << object.additiveBlend << ',' << csv(object.initialAnimation)
            << ',' << object.initialAnimationLoops << ',' << object.position.x
            << ',' << object.position.y << ',' << object.position.z << ','
            << object.rotation.x << ',' << object.rotation.y << ','
            << object.rotation.z << ',' << object.rotation.w << ','
            << object.scale.x << ',' << object.scale.y << ','
            << object.scale.z;
        for (const float component : object.worldTransform) {
            objectAssetLog_ << ',' << component;
        }
        objectAssetLog_
            << ',' << object.electricOffDurationMilliseconds << ','
            << object.electricOnDurationMilliseconds << ','
            << object.electricReadyDurationMilliseconds << ','
            << object.electricDelayMilliseconds << ',' << object.electricDamage
            << ',' << object.electricInitiallyActive << ','
            << object.electricInitialState << ','
            << object.platformParkDurationMilliseconds << ','
            << object.platformLineSpeedCentimetersPerMillisecond << ','
            << object.platformInitiallyActive << ','
            << object.platformActiveForever << ','
            << object.platformLinkedWaypointId << ','
            << object.trainLineSpeedCentimetersPerMillisecond << ','
            << object.trainInitiallyActive << ','
            << object.trainLinkedWaypointId << ','
            << object.trainPreviousObjectId << ','
            << object.trainNextObjectId << ','
            << object.trainLifeDurationMilliseconds << ','
            << object.trainCanTransport << ','
            << object.trainKillsPlayer << ','
            << object.areaDamageType << ','
            << object.areaDamageBeginDelayMilliseconds << ','
            << object.areaDamageRandomLowMilliseconds << ','
            << object.areaDamageRandomHighMilliseconds << ','
            << object.areaDamageIgnorePhysics << ','
            << object.areaDamageActiveForever << ','
            << object.areaDamageAutomaticDetection << ','
            << object.damage << ','
            << object.collisionLocalMinimum.x << ','
            << object.collisionLocalMinimum.y << ','
            << object.collisionLocalMinimum.z << ','
            << object.collisionLocalMaximum.x << ','
            << object.collisionLocalMaximum.y << ','
            << object.collisionLocalMaximum.z << ','
            << object.hasCollisionBounds << '\n';
    }
    objectAssetLog_.flush();
    for (const game::CameraArea& area : level.cameraAreas()) {
        cameraAreaAssetLog_ << area.objectId;
        for (const std::int32_t nextAreaId : area.nextAreaIds) {
            cameraAreaAssetLog_ << ',' << nextAreaId;
        }
        for (const std::uint32_t switchTime : area.switchTimeUnits) {
            cameraAreaAssetLog_ << ',' << switchTime;
        }
        cameraAreaAssetLog_ << ',' << area.inverseNormal << ',' << area.height
                            << ',' << area.zFollowRate << ',' << area.disabled
                            << ',' << area.farPlaneOffset;
        for (const game::CameraControlPoint& point : area.controlPoints) {
            cameraAreaAssetLog_
                << ',' << point.objectId << ',' << point.position.x << ','
                << point.position.y << ',' << point.position.z << ','
                << point.direction.x << ',' << point.direction.y << ','
                << point.direction.z << ',' << point.distance << ','
                << point.targetOffset.x << ',' << point.targetOffset.y << ','
                << point.targetOffset.z << ',' << point.targetHeightOffset;
        }
        cameraAreaAssetLog_
            << ',' << csv(visibleRoomList(area.mustInvisibleRooms)) << ','
            << csv(visibleRoomList(area.mustVisibleRooms)) << '\n';
    }
    cameraAreaAssetLog_.flush();
    for (const game::LevelTriggerAsset& trigger : level.triggers()) {
        triggerAssetLog_
            << trigger.objectId << ',' << csv(trigger.name) << ','
            << trigger.roomId << ',' << trigger.enabled << ','
            << trigger.autoDisabled << ',' << trigger.orientedBox << ','
            << trigger.outToInCinematicId << ','
            << trigger.inToOutCinematicId << ','
            << trigger.whileInsideCinematicId << ','
            << trigger.whileOutsideCinematicId << ',' << trigger.position.x
            << ',' << trigger.position.y << ',' << trigger.position.z << ','
            << trigger.rotation.x << ',' << trigger.rotation.y << ','
            << trigger.rotation.z << ',' << trigger.rotation.w << ','
            << trigger.scale.x << ',' << trigger.scale.y << ','
            << trigger.scale.z << ',' << trigger.sizes.x << ','
            << trigger.sizes.y << ',' << trigger.sizes.z;
        for (const float component : trigger.worldTransform) {
            triggerAssetLog_ << ',' << component;
        }
        triggerAssetLog_ << '\n';
    }
    triggerAssetLog_.flush();
    for (const game::LevelWayPointAsset& waypoint : level.waypoints()) {
        waypointAssetLog_
            << waypoint.objectId << ',' << csv(waypoint.name) << ','
            << waypoint.roomId << ',' << waypoint.position.x << ','
            << waypoint.position.y << ','
            << waypoint.position.z << ',' << waypoint.enabled << ','
            << waypoint.electricShock << ',' << waypoint.nextWaypointIds[0]
            << ',' << waypoint.nextWaypointIds[1] << ','
            << waypoint.useGravityWhenEnd << ',' << waypoint.unstandable
            << ',' << waypoint.jumpDirection << ',' << waypoint.timeToMe
            << ',' << waypoint.linkedCameraAreaId << '\n';
    }
    waypointAssetLog_.flush();
    for (const game::LevelWebGrabPointAsset& point : level.webGrabPoints()) {
        webGrabPointAssetLog_
            << point.objectId << ',' << point.roomId << ','
            << point.position.x << ','
            << point.position.y << ',' << point.position.z << ','
            << point.directionControlPointId << ',' << point.direction.x
            << ',' << point.direction.y << ',' << point.direction.z << ','
            << point.length << ',' << point.visibleLength << ','
            << point.verticalAngleDegrees << ','
            << point.horizontalAngleDegrees << ',' << point.exitSpeed << ','
            << point.cannotControl << ',' << point.targetWaypointId << ','
            << point.targetWaypointPosition.x << ','
            << point.targetWaypointPosition.y << ','
            << point.targetWaypointPosition.z << ',' << point.targetSlideId
            << '\n';
    }
    webGrabPointAssetLog_.flush();
    for (const game::LevelSlideAsset& slide : level.slides()) {
        std::string waypointIds;
        for (const std::int32_t waypointId : slide.waypointIds) {
            if (!waypointIds.empty()) {
                waypointIds += '|';
            }
            waypointIds += std::to_string(waypointId);
        }
        slideAssetLog_ << slide.objectId << ',' << csv(slide.name) << ','
                       << slide.roomId << ',' << slide.position.x << ','
                       << slide.position.y << ','
                       << slide.position.z << ',' << slide.linkedWaypointId
                       << ',' << slide.enabled << ',' << slide.electricShock
                       << ',' << csv(waypointIds) << '\n';
    }
    slideAssetLog_.flush();
    for (const game::LevelCheckPointAsset& checkpoint :
         level.checkPoints()) {
        checkPointAssetLog_
            << checkpoint.objectId << ',' << checkpoint.roomId << ','
            << checkpoint.position.x << ',' << checkpoint.position.y << ','
            << checkpoint.position.z << ',' << checkpoint.rotation.x << ','
            << checkpoint.rotation.y << ',' << checkpoint.rotation.z << ','
            << checkpoint.rotation.w << ',' << checkpoint.scale.x << ','
            << checkpoint.scale.y << ',' << checkpoint.scale.z << ','
            << checkpoint.sizes.x << ',' << checkpoint.sizes.y << ','
            << checkpoint.sizes.z << ',' << checkpoint.savePosition << ','
            << checkpoint.enabled << ',' << checkpoint.orientedBox << ','
            << checkpoint.linkedWaypointId;
        for (const float component : checkpoint.worldTransform) {
            checkPointAssetLog_ << ',' << component;
        }
        checkPointAssetLog_ << '\n';
    }
    checkPointAssetLog_.flush();
    const auto primitiveName = [](assets::ColladaPrimitive primitive) {
        switch (primitive) {
        case assets::ColladaPrimitive::Triangles:
            return "triangles";
        case assets::ColladaPrimitive::TriangleStrip:
            return "triangle_strip";
        case assets::ColladaPrimitive::Lines:
            return "lines";
        case assets::ColladaPrimitive::LineStrip:
            return "line_strip";
        case assets::ColladaPrimitive::LineLoop:
            return "line_loop";
        }
        return "unknown";
    };
    const auto recordMesh = [&](std::string_view ownerKind,
                                std::int32_t ownerId,
                                std::string_view ownerName,
                                std::int32_t roomId,
                                bool ownerAdditive,
                                std::string_view meshSource,
                                const assets::ColladaMeshFile& mesh,
                                std::span<const assets::BtexTexture> textures) {
        const auto imageField = [&](const std::optional<std::uint32_t>& index,
                                    bool name) {
            if (!index || *index >= mesh.images().size()) {
                return std::string{};
            }
            return name ? mesh.images()[*index].name
                        : mesh.images()[*index].sourcePath;
        };
        const auto textureAlpha = [&](const std::optional<std::uint32_t>& index) {
            return index && *index < textures.size()
                       ? std::to_string(textures[*index].containsAlpha())
                       : std::string{};
        };
        const auto imageIndex = [](const std::optional<std::uint32_t>& index) {
            return index ? std::to_string(*index) : std::string{};
        };
        for (std::size_t textureIndex = 0; textureIndex < textures.size();
             ++textureIndex) {
            const assets::BtexTexture& texture = textures[textureIndex];
            if (texture.mipLevels().empty()) {
                continue;
            }
            const assets::RgbaImage& image = texture.mipLevels().front();
            std::array<std::uint32_t, 4> minimum{255, 255, 255, 255};
            std::array<std::uint32_t, 4> maximum{};
            std::array<std::uint64_t, 4> sum{};
            std::uint64_t alphaZero = 0;
            std::uint64_t alphaPartial = 0;
            std::uint64_t alphaOpaque = 0;
            std::uint64_t black = 0;
            std::uint64_t opaqueBlack = 0;
            for (std::size_t offset = 0; offset + 3 < image.pixels.size();
                 offset += 4) {
                for (std::size_t channel = 0; channel < 4; ++channel) {
                    const std::uint32_t value = image.pixels[offset + channel];
                    minimum[channel] = std::min(minimum[channel], value);
                    maximum[channel] = std::max(maximum[channel], value);
                    sum[channel] += value;
                }
                const std::uint8_t alpha = image.pixels[offset + 3];
                alphaZero += alpha == 0;
                alphaPartial += alpha != 0 && alpha != 255;
                alphaOpaque += alpha == 255;
                const bool isBlack = image.pixels[offset] <= 8 &&
                                     image.pixels[offset + 1] <= 8 &&
                                     image.pixels[offset + 2] <= 8;
                black += isBlack;
                opaqueBlack += isBlack && alpha == 255;
            }
            const std::uint64_t pixelCount =
                static_cast<std::uint64_t>(image.width) * image.height;
            const auto mean = [pixelCount](std::uint64_t total) {
                return pixelCount == 0
                           ? 0.0
                           : static_cast<double>(total) /
                                 static_cast<double>(pixelCount);
            };
            const std::string imageName =
                textureIndex < mesh.images().size()
                    ? mesh.images()[textureIndex].name
                    : std::string{};
            const std::string imagePath =
                textureIndex < mesh.images().size()
                    ? mesh.images()[textureIndex].sourcePath
                    : std::string{};
            textureAssetLog_
                << csv(ownerKind) << ',' << ownerId << ',' << csv(ownerName)
                << ',' << csv(meshSource) << ',' << textureIndex << ','
                << csv(imageName) << ',' << csv(imagePath) << ','
                << image.width << ',' << image.height << ','
                << texture.containsAlpha() << ',' << minimum[0] << ','
                << maximum[0] << ',' << mean(sum[0]) << ',' << minimum[1]
                << ',' << maximum[1] << ',' << mean(sum[1]) << ','
                << minimum[2] << ',' << maximum[2] << ',' << mean(sum[2])
                << ',' << minimum[3] << ',' << maximum[3] << ','
                << mean(sum[3]) << ',' << alphaZero << ',' << alphaPartial
                << ',' << alphaOpaque << ',' << black << ',' << opaqueBlack
                << '\n';
        }
        for (std::size_t nodeIndex = 0; nodeIndex < mesh.sceneNodes().size();
             ++nodeIndex) {
            const assets::ColladaSceneNode& node = mesh.sceneNodes()[nodeIndex];
            std::string geometryIndices;
            for (const std::uint32_t geometryIndex : node.geometryIndices) {
                if (!geometryIndices.empty()) {
                    geometryIndices += '|';
                }
                geometryIndices += std::to_string(geometryIndex);
            }
            colladaNodeAssetLog_
                << csv(ownerKind) << ',' << ownerId << ',' << csv(ownerName)
                << ',' << csv(meshSource) << ',' << nodeIndex << ','
                << node.parentIndex << ',' << csv(node.id) << ','
                << csv(node.name) << ',' << csv(node.scopeId) << ','
                << node.position.x << ',' << node.position.y << ','
                << node.position.z << ',' << node.rotation.x << ','
                << node.rotation.y << ',' << node.rotation.z << ','
                << node.rotation.w << ',' << node.scale.x << ','
                << node.scale.y << ',' << node.scale.z << ','
                << node.worldPosition.x << ',' << node.worldPosition.y << ','
                << node.worldPosition.z;
            for (const float component : node.worldLinear) {
                colladaNodeAssetLog_ << ',' << component;
            }
            colladaNodeAssetLog_ << ',' << csv(geometryIndices) << '\n';
        }
        for (const assets::ColladaGeometry& geometry :
             mesh.sceneGeometries()) {
            for (std::size_t bufferIndex = 0;
                 bufferIndex < geometry.meshBuffers.size(); ++bufferIndex) {
                const assets::ColladaMeshBuffer& buffer =
                    geometry.meshBuffers[bufferIndex];
                const assets::ColladaMaterial* material =
                    mesh.findMaterial(buffer.materialName);
                std::uint32_t alphaMinimum = 255;
                std::uint32_t alphaMaximum = 0;
                assets::AxisAlignedBounds indexedBounds{};
                bool hasIndexedVertex = false;
                for (const std::uint16_t vertexIndex : buffer.indices) {
                    if (vertexIndex >= geometry.vertices.size()) {
                        continue;
                    }
                    const assets::ColladaVertex& vertex =
                        geometry.vertices[vertexIndex];
                    const std::uint32_t alpha = vertex.color >> 24U;
                    alphaMinimum = std::min(alphaMinimum, alpha);
                    alphaMaximum = std::max(alphaMaximum, alpha);
                    if (!hasIndexedVertex) {
                        indexedBounds = {vertex.position, vertex.position};
                        hasIndexedVertex = true;
                    } else {
                        indexedBounds.minimum.x = std::min(
                            indexedBounds.minimum.x, vertex.position.x);
                        indexedBounds.minimum.y = std::min(
                            indexedBounds.minimum.y, vertex.position.y);
                        indexedBounds.minimum.z = std::min(
                            indexedBounds.minimum.z, vertex.position.z);
                        indexedBounds.maximum.x = std::max(
                            indexedBounds.maximum.x, vertex.position.x);
                        indexedBounds.maximum.y = std::max(
                            indexedBounds.maximum.y, vertex.position.y);
                        indexedBounds.maximum.z = std::max(
                            indexedBounds.maximum.z, vertex.position.z);
                    }
                }
                if (buffer.indices.empty()) {
                    alphaMinimum = 0;
                }
                if (!hasIndexedVertex) {
                    indexedBounds = geometry.bounds;
                }
                const std::optional<std::uint32_t> noImage;
                const auto& diffuse =
                    material == nullptr ? noImage : material->diffuseImageIndex;
                const auto& secondary = material == nullptr
                                            ? noImage
                                            : material->secondaryImageIndex;
                const auto& lightmap =
                    material == nullptr ? noImage : material->lightmapImageIndex;
                materialAssetLog_
                    << csv(ownerKind) << ',' << ownerId << ','
                    << csv(ownerName) << ',' << roomId << ','
                    << ownerAdditive << ','
                    << csv(meshSource) << ',' << csv(geometry.name) << ','
                    << bufferIndex << ',' << csv(buffer.materialName) << ','
                    << csv(material == nullptr ? std::string_view{}
                                               : material->id)
                    << ','
                    << csv(material == nullptr ? std::string_view{}
                                               : material->effectId)
                    << ',' << primitiveName(buffer.primitive) << ','
                    << geometry.vertices.size() << ',' << buffer.indices.size()
                    << ',' << imageIndex(diffuse) << ','
                    << csv(imageField(diffuse, true)) << ','
                    << csv(imageField(diffuse, false)) << ','
                    << textureAlpha(diffuse) << ',' << imageIndex(secondary)
                    << ',' << csv(imageField(secondary, true)) << ','
                    << csv(imageField(secondary, false)) << ','
                    << textureAlpha(secondary) << ',' << imageIndex(lightmap)
                    << ',' << csv(imageField(lightmap, true)) << ','
                    << csv(imageField(lightmap, false)) << ','
                    << textureAlpha(lightmap) << ','
                    << (material == nullptr ? 0U
                                            : material->secondaryTextureMode)
                    << ','
                    << (material != nullptr && material->additiveBlend) << ','
                    << (material != nullptr && material->backFaceCulling)
                    << ','
                    << (material != nullptr && material->frontFaceCulling)
                    << ','
                    << (material != nullptr &&
                        material->transparentAlphaChannel)
                    << ','
                    << (material == nullptr ? 0.0F
                                            : material->materialTypeParameter)
                    << ',' << buffer.usesSecondaryTextureCoordinates << ','
                    << alphaMinimum << ',' << alphaMaximum << ','
                    << indexedBounds.minimum.x << ','
                    << indexedBounds.minimum.y << ','
                    << indexedBounds.minimum.z << ','
                    << indexedBounds.maximum.x << ','
                    << indexedBounds.maximum.y << ','
                    << indexedBounds.maximum.z << '\n';

                if (ownerKind == "sky") {
                    std::size_t triangleIndex = 0;
                    const auto recordTriangle =
                        [&](std::uint16_t firstIndex,
                            std::uint16_t secondIndex,
                            std::uint16_t thirdIndex) {
                            if (firstIndex >= geometry.vertices.size() ||
                                secondIndex >= geometry.vertices.size() ||
                                thirdIndex >= geometry.vertices.size()) {
                                return;
                            }
                            const auto writeVertex = [&](std::uint16_t index) {
                                const assets::ColladaVertex& vertex =
                                    geometry.vertices[index];
                                skyTriangleAssetLog_
                                    << ',' << vertex.position.x << ','
                                    << vertex.position.y << ','
                                    << vertex.position.z << ','
                                    << vertex.textureCoordinate[0] << ','
                                    << vertex.textureCoordinate[1];
                            };
                            skyTriangleAssetLog_
                                << csv(geometry.name) << ',' << bufferIndex
                                << ',' << csv(buffer.materialName) << ','
                                << triangleIndex++;
                            writeVertex(firstIndex);
                            writeVertex(secondIndex);
                            writeVertex(thirdIndex);
                            skyTriangleAssetLog_ << '\n';
                        };
                    if (buffer.primitive ==
                        assets::ColladaPrimitive::Triangles) {
                        for (std::size_t index = 0;
                             index + 2 < buffer.indices.size(); index += 3) {
                            recordTriangle(buffer.indices[index],
                                           buffer.indices[index + 1],
                                           buffer.indices[index + 2]);
                        }
                    } else if (buffer.primitive ==
                               assets::ColladaPrimitive::TriangleStrip) {
                        for (std::size_t index = 2;
                             index < buffer.indices.size(); ++index) {
                            const bool odd = (index & 1U) != 0;
                            recordTriangle(
                                buffer.indices[index - (odd ? 0 : 2)],
                                buffer.indices[index - 1],
                                buffer.indices[index - (odd ? 2 : 0)]);
                        }
                    }
                }
            }
        }
    };

    for (std::size_t roomIndex = 0; roomIndex < level.rooms().size();
         ++roomIndex) {
        const game::LevelRoomAsset& room = level.rooms()[roomIndex];
        recordMesh("room", static_cast<std::int32_t>(roomIndex + 1),
                   room.name, static_cast<std::int32_t>(roomIndex + 1),
                   false, room.sceneFile, room.geometry, room.textures);
        // Collision BDAEs retain their own visual-scene node names. Native
        // PhysicsTriangleMeshShape uses those names to assign wall, jump-wall,
        // and edge flags, so preserving them in the census is necessary to
        // audit traversal even though the collision mesh is never rendered.
        recordMesh("room_collision",
                   static_cast<std::int32_t>(roomIndex + 1), room.name,
                   static_cast<std::int32_t>(roomIndex + 1), false,
                   "collision", room.collision,
                   std::span<const assets::BtexTexture>{});
    }
    const game::LevelStaticMeshAsset& sky = level.introSky();
    recordMesh("sky", -1, sky.name, 0, false, sky.name, sky.geometry,
               sky.textures);
    recordMesh("player", level.player().objectId,
               level.player().sceneNodeName, 0, false, "player",
               level.player().mesh, level.player().textures);
    for (const game::LevelObjectAsset& object : level.objects()) {
        if (object.archetypeIndex >= level.objectArchetypes().size()) {
            continue;
        }
        const game::LevelObjectArchetypeAsset& archetype =
            level.objectArchetypes()[object.archetypeIndex];
        recordMesh("object", object.objectId, object.name, object.roomId,
                   object.additiveBlend, archetype.meshFile, archetype.mesh,
                   archetype.textures);
    }
    for (const game::LevelEnemyAsset& enemy : level.enemies()) {
        if (enemy.archetypeIndex >= level.enemyArchetypes().size()) {
            continue;
        }
        const game::EnemyArchetypeAsset& archetype =
            level.enemyArchetypes()[enemy.archetypeIndex];
        recordMesh("enemy", enemy.objectId, enemy.name, enemy.roomId,
                   false, archetype.meshFile, archetype.mesh,
                   archetype.textures);
    }
    for (const game::CinematicActorAsset& actor : level.introActors()) {
        recordMesh("intro_actor", actor.objectId, actor.sceneNodeName, 0, false,
                   actor.sceneNodeName, actor.mesh, actor.textures);
    }
    for (const game::PlayerHitEffectAsset& effect :
         level.playerHitEffects()) {
        // Player::LoadHitEffects (0x00344ab4) preloads this complete table.
        // Keep its source materials and decoded texture alpha in every asset
        // census so CAnimObjEffect's 0x1d/0x1e override can be audited from
        // shipped data rather than renderer screenshots.
        recordMesh("player_hit_effect", effect.definition.id,
                   effect.definition.name, 0, false,
                   effect.definition.meshFile, effect.mesh,
                   effect.textures);
    }
    for (const game::LevelCinematicAsset& cinematic : level.cinematics()) {
        for (const game::CinematicActorAsset& actor : cinematic.actors) {
            recordMesh("cinematic_actor", actor.objectId,
                       std::to_string(cinematic.objectId) + ":" +
                           actor.sceneNodeName,
                       0, false, actor.sceneNodeName, actor.mesh,
                       actor.textures);
        }
    }
    materialAssetLog_.flush();
    textureAssetLog_.flush();
    skyTriangleAssetLog_.flush();
    colladaNodeAssetLog_.flush();
}

void AutoplayHarness::recordPlayerStateAssets(
    const game::PlayerStateConfigDatabase& states) {
    if (!playerStateAssetLog_) {
        return;
    }
    for (const game::PlayerStateDefinition& state : states.states()) {
        std::string animationIds;
        for (const std::int16_t animationId : state.animationIds) {
            if (!animationIds.empty()) {
                animationIds += '|';
            }
            animationIds += std::to_string(animationId);
        }
        std::array<std::string, 3> transitions;
        for (std::size_t field = 0; field < transitions.size(); ++field) {
            for (const std::int16_t value : state.transitionFields[field]) {
                if (!transitions[field].empty()) {
                    transitions[field] += '|';
                }
                transitions[field] += std::to_string(value);
            }
        }
        std::array<std::string, 2> auxiliaryIds;
        for (std::size_t field = 0; field < auxiliaryIds.size(); ++field) {
            for (const std::int16_t value : state.auxiliaryIdLists[field]) {
                if (!auxiliaryIds[field].empty()) {
                    auxiliaryIds[field] += '|';
                }
                auxiliaryIds[field] += std::to_string(value);
            }
        }
        playerStateAssetLog_
            << state.id << ',' << csv(state.name) << ',' << state.stateClass
            << ',' << state.motionType << ',' << state.motionParameters[0]
            << ',' << state.motionParameters[1] << ','
            << state.motionParameters[2] << ',' << state.motionParameters[3]
            << ',' << state.primaryAnimationId << ',' << csv(animationIds)
            << ',' << state.soundTriggerFrame << ',' << state.nextStateId
            << ',' << state.timingParameters[0] << ','
            << state.timingParameters[1] << ','
            << state.auxiliaryParameters[0] << ','
            << state.auxiliaryParameters[1] << ','
            << state.auxiliaryParameters[2] << ','
            << state.auxiliaryParameters[3] << ','
            << csv(auxiliaryIds[0]) << ',' << csv(auxiliaryIds[1]) << ','
            << csv(transitions[0]) << ','
            << csv(transitions[1]) << ',' << csv(transitions[2]) << '\n';
    }
    playerStateAssetLog_.flush();
}

void AutoplayHarness::recordCinematicAssets(
    std::span<const game::LevelCinematicAsset> cinematics) {
    if (!cinematicAssetLog_) {
        return;
    }
    for (const game::LevelCinematicAsset& cinematic : cinematics) {
        for (const game::CinematicThread& thread :
             cinematic.script.threads()) {
            if (thread.commands.empty()) {
                cinematicAssetLog_
                    << cinematic.objectId << ',' << csv(cinematic.name) << ','
                    << csv(cinematic.scriptFile) << ',' << thread.type << ','
                    << thread.objectId << ',' << csv(thread.name)
                    << ",,,,,\n";
                continue;
            }
            for (const game::CinematicCommand& command : thread.commands) {
                std::string attributes;
                for (const game::CinematicAttribute& attribute :
                     command.attributes) {
                    if (!attributes.empty()) {
                        attributes += ';';
                    }
                    attributes += attribute.type + ":" + attribute.name +
                                  "=" + attribute.value;
                }
                cinematicAssetLog_
                    << cinematic.objectId << ',' << csv(cinematic.name) << ','
                    << csv(cinematic.scriptFile) << ',' << thread.type << ','
                    << thread.objectId << ',' << csv(thread.name) << ','
                    << command.timestampMilliseconds << ',' << command.id
                    << ',' << csv(command.name) << ',' << csv(attributes)
                    << '\n';
            }
        }
    }
    cinematicAssetLog_.flush();
}

void AutoplayHarness::recordAudio(std::uint64_t timeMilliseconds,
                                  std::string_view action,
                                  std::string_view eventName, bool loop,
                                  bool spatial, float volume) {
    if (action == "play") {
        playedAudioEvents_.emplace(eventName);
        ++audioPlayCounts_[std::string(eventName)];
    } else if (action == "stop") {
        stoppedAudioEvents_.emplace(eventName);
        ++audioStopCounts_[std::string(eventName)];
    }
    recordEvent(timeMilliseconds, "audio",
                "action=" + std::string(action) + ";event=" +
                    std::string(eventName) + ";loop=" +
                    std::to_string(loop) + ";spatial=" +
                    std::to_string(spatial) + ";volume=" +
                    std::to_string(volume));
}

bool AutoplayHarness::periodicCaptureDue(
    std::uint64_t timeMilliseconds) noexcept {
    if (captureIntervalMilliseconds_ == 0 ||
        timeMilliseconds < nextCaptureMilliseconds_) {
        return false;
    }
    nextCaptureMilliseconds_ = timeMilliseconds + captureIntervalMilliseconds_;
    return true;
}

Result AutoplayHarness::writeCapture(const assets::RgbaImage& image,
                                     std::string_view label,
                                     std::uint64_t timeMilliseconds) {
    if (image.width == 0 || image.height == 0 ||
        image.pixels.size() !=
            static_cast<std::size_t>(image.width) * image.height * 4U) {
        return Result::failure("Autoplay capture has invalid image data");
    }
    const std::string filename =
        std::to_string(timeMilliseconds) + "-" + sanitizeLabel(label) + ".bmp";
    std::ofstream stream(outputPath_ / filename,
                         std::ios::binary | std::ios::trunc);
    if (!stream) {
        return Result::failure("Could not create autoplay capture " + filename);
    }
    constexpr std::uint32_t fileHeaderSize = 14;
    constexpr std::uint32_t infoHeaderSize = 40;
    const std::uint32_t pixelBytes = image.width * image.height * 4U;
    stream.put('B');
    stream.put('M');
    writeU32(stream, fileHeaderSize + infoHeaderSize + pixelBytes);
    writeU16(stream, 0);
    writeU16(stream, 0);
    writeU32(stream, fileHeaderSize + infoHeaderSize);
    writeU32(stream, infoHeaderSize);
    writeU32(stream, image.width);
    writeU32(stream, static_cast<std::uint32_t>(-
        static_cast<std::int32_t>(image.height)));
    writeU16(stream, 1);
    writeU16(stream, 32);
    writeU32(stream, 0);
    writeU32(stream, pixelBytes);
    writeU32(stream, 0);
    writeU32(stream, 0);
    writeU32(stream, 0);
    writeU32(stream, 0);
    for (std::size_t offset = 0; offset < image.pixels.size(); offset += 4) {
        stream.put(static_cast<char>(image.pixels[offset + 2]));
        stream.put(static_cast<char>(image.pixels[offset + 1]));
        stream.put(static_cast<char>(image.pixels[offset]));
        stream.put(static_cast<char>(image.pixels[offset + 3]));
    }
    if (!stream) {
        return Result::failure("Could not write autoplay capture " + filename);
    }
    recordEvent(timeMilliseconds, "capture", filename);
    return Result::success();
}

void AutoplayHarness::finish(bool applicationSucceeded,
                             std::string_view detail) {
    if (finishedLog_) {
        return;
    }
    if (!applicationSucceeded && !failed_) {
        failed_ = true;
        failureMessage_ = detail.empty()
            ? "application failed before autoplay completed"
            : std::string(detail);
        if (eventLog_) {
            recordEvent(lastTimeMilliseconds_, "application_failure",
                        failureMessage_);
        }
    }
    finishedLog_ = true;
    std::ofstream summary(outputPath_ / "summary.txt", std::ios::trunc);
    summary << "application_succeeded=" << applicationSucceeded << '\n'
            << "harness_complete=" << complete_ << '\n'
            << "harness_failed=" << failed_ << '\n'
            << "last_frame=" << lastFrameIndex_ << '\n'
            << "last_time_ms=" << lastTimeMilliseconds_ << '\n'
            << "completed_steps=" << activeStepIndex_ << '\n'
            << "total_steps=" << steps_.size() << '\n'
            << "failure=" << failureMessage_ << '\n'
            << "detail=" << detail << '\n';
}

std::string AutoplayHarness::stepName(StepKind kind) {
    switch (kind) {
    case StepKind::WaitGameplay: return "wait_gameplay";
    case StepKind::WaitIntro: return "wait_intro";
    case StepKind::WaitControls: return "wait_controls";
    case StepKind::Wait: return "wait";
    case StepKind::MoveTo: return "move_to";
    case StepKind::MoveTo3D: return "move_to_3d";
    case StepKind::ClimbTo: return "climb_to";
    case StepKind::MoveInput: return "move_input";
    case StepKind::MoveUntilWall: return "move_until_wall";
    case StepKind::MoveUntilState: return "move_until_state";
    case StepKind::MoveToUntilState: return "move_to_until_state";
    case StepKind::MoveUntilCinematic: return "move_until_cinematic";
    case StepKind::MoveInputUntilCinematic:
        return "move_input_until_cinematic";
    case StepKind::WaitCinematicStarted: return "wait_cinematic_started";
    case StepKind::WaitTransportState: return "wait_transport_state";
    case StepKind::WaitDeathScreen: return "wait_death_screen";
    case StepKind::WaitDeathConfirmation:
        return "wait_death_confirmation";
    case StepKind::WaitExitMenu: return "wait_exit_menu";
    case StepKind::WaitMainMenuRequested:
        return "wait_main_menu_requested";
    case StepKind::CrossTrigger: return "cross_trigger";
    case StepKind::WaitEnemiesGrounded: return "wait_enemies_grounded";
    case StepKind::WaitEnemiesActive: return "wait_enemies_active";
    case StepKind::WaitEnemyMeleeAttack: return "wait_enemy_melee_attack";
    case StepKind::WaitEnemyProjectile: return "wait_enemy_projectile";
    case StepKind::SetEnemyAi: return "set_enemy_ai";
    case StepKind::SetEnemyPhysics: return "set_enemy_physics";
    case StepKind::DamageEnemy: return "damage_enemy";
    case StepKind::WaitEnemyAnimation: return "wait_enemy_animation";
    case StepKind::WaitObjectElectricState:
        return "wait_object_electric_state";
    case StepKind::WaitAreaDamageState:
        return "wait_area_damage_state";
    case StepKind::WaitObjectNear: return "wait_object_near";
    case StepKind::WaitLevelEnd: return "wait_level_end";
    case StepKind::Attack: return "attack";
    case StepKind::AttackObject: return "attack_object";
    case StepKind::CollectBonus: return "collect_bonus";
    case StepKind::RescueHostage: return "rescue_hostage";
    case StepKind::WaitDropHit: return "wait_drop_hit";
    case StepKind::CollectComic: return "collect_comic";
    case StepKind::Jump: return "jump";
    case StepKind::Punch: return "punch";
    case StepKind::PressButtons: return "press_buttons";
    case StepKind::PunchWhenReady: return "punch_when_ready";
    case StepKind::PunchAttackWhenReady: return "punch_attack_when_ready";
    case StepKind::JumpAttackWhenReady: return "jump_attack_when_ready";
    case StepKind::JumpReleaseAttackWhenReady:
        return "jump_release_attack_when_ready";
    case StepKind::WebAttackWhenReady: return "web_attack_when_ready";
    case StepKind::WebHeldAttackWhenReady:
        return "web_held_attack_when_ready";
    case StepKind::SpiderSense: return "spider_sense";
    case StepKind::SuperAttack: return "super_attack";
    case StepKind::WebOn: return "web_on";
    case StepKind::WebOff: return "web_off";
    case StepKind::SetAutoQuickTime: return "set_auto_qte";
    case StepKind::QuickTimeTap: return "qte_tap";
    case StepKind::MenuUp: return "menu_up";
    case StepKind::MenuDown: return "menu_down";
    case StepKind::MenuSelect: return "menu_select";
    case StepKind::Teleport: return "teleport";
    case StepKind::StartCinematic: return "start_cinematic";
    case StepKind::Capture: return "capture";
    case StepKind::AssertNear: return "assert_near";
    case StepKind::AssertCameraArea: return "assert_camera_area";
    case StepKind::AssertLastCheckPoint: return "assert_last_checkpoint";
    case StepKind::AssertEnemyNear: return "assert_enemy_near";
    case StepKind::AssertEnemyDistanceAbove:
        return "assert_enemy_distance_above";
    case StepKind::AssertEnemyHealthBelow:
        return "assert_enemy_health_below";
    case StepKind::AssertEnemyHealthNear:
        return "assert_enemy_health_near";
    case StepKind::AssertEnemyMeleeAttackActive:
        return "assert_enemy_melee_attack_active";
    case StepKind::AssertEnemyBehavior:
        return "assert_enemy_behavior";
    case StepKind::AssertObjectDestroyed: return "assert_object_destroyed";
    case StepKind::AssertObjectHidden: return "assert_object_hidden";
    case StepKind::AssertObjectAnimation: return "assert_object_animation";
    case StepKind::AssertObjectElectricState:
        return "assert_object_electric_state";
    case StepKind::AssertObjectNear: return "assert_object_near";
    case StepKind::AssertComicCollected: return "assert_comic_collected";
    case StepKind::AssertBonusCollected: return "assert_bonus_collected";
    case StepKind::AssertHostageFreed: return "assert_hostage_freed";
    case StepKind::AssertSkillPointsAtLeast:
        return "assert_skill_points_at_least";
    case StepKind::AssertComboScoreAtLeast:
        return "assert_combo_score_at_least";
    case StepKind::AssertPlayerState: return "assert_player_state";
    case StepKind::AssertPlayerEffect: return "assert_player_effect";
    case StepKind::AssertPlayerWebLine:
        return "assert_player_web_line";
    case StepKind::AssertSlowMotion: return "assert_slow_motion";
    case StepKind::AssertHealthAbove: return "assert_health_above";
    case StepKind::AssertGameplayUi: return "assert_gameplay_ui";
    case StepKind::AssertHealthBelow: return "assert_health_below";
    case StepKind::AssertCinematicNotStarted:
        return "assert_cinematic_not_started";
    case StepKind::AssertDeathScreenActive:
        return "assert_death_screen_active";
    case StepKind::AssertDeathScreenInactive:
        return "assert_death_screen_inactive";
    case StepKind::AssertDeathAlphaAbove:
        return "assert_death_alpha_above";
    case StepKind::AssertDeathConfirmationActive:
        return "assert_death_confirmation_active";
    case StepKind::AssertDeathConfirmationInactive:
        return "assert_death_confirmation_inactive";
    case StepKind::AssertDeathConfirmationSelection:
        return "assert_death_confirmation_selection";
    case StepKind::AssertAudioPlayed: return "assert_audio_played";
    case StepKind::AssertAudioNotPlayed: return "assert_audio_not_played";
    case StepKind::AssertAudioStopped: return "assert_audio_stopped";
    case StepKind::AssertAudioPlayCount: return "assert_audio_play_count";
    case StepKind::AssertAudioStopCount: return "assert_audio_stop_count";
    case StepKind::AssertEventNotObserved:
        return "assert_event_not_observed";
    case StepKind::Finish: return "finish";
    }
    return "unknown";
}

std::string AutoplayHarness::behaviorName(game::EnemyBehaviorState behavior) {
    switch (behavior) {
    case game::EnemyBehaviorState::Disabled: return "disabled";
    case game::EnemyBehaviorState::Idle: return "idle";
    case game::EnemyBehaviorState::Chasing: return "chasing";
    case game::EnemyBehaviorState::AttackRange: return "attack_range";
    case game::EnemyBehaviorState::Hurt: return "hurt";
    case game::EnemyBehaviorState::TiedUp: return "tied_up";
    case game::EnemyBehaviorState::Dead: return "dead";
    }
    return "unknown";
}

std::string AutoplayHarness::rhinoTaskName(
    game::RhinoBossTaskState task) {
    switch (task) {
    case game::RhinoBossTaskState::None: return "none";
    case game::RhinoBossTaskState::Approach: return "approach";
    case game::RhinoBossTaskState::Melee: return "melee";
    case game::RhinoBossTaskState::DashReady: return "dash_ready";
    case game::RhinoBossTaskState::DashRush: return "dash_rush";
    case game::RhinoBossTaskState::DashSuccess: return "dash_success";
    case game::RhinoBossTaskState::DashSkid: return "dash_skid";
    case game::RhinoBossTaskState::DashFailed: return "dash_failed";
    case game::RhinoBossTaskState::DashFailedStruggle:
        return "dash_failed_struggle";
    case game::RhinoBossTaskState::ThrowApproach: return "throw_approach";
    case game::RhinoBossTaskState::ThrowReady: return "throw_ready";
    case game::RhinoBossTaskState::ThrowRush: return "throw_rush";
    case game::RhinoBossTaskState::ThrowCatch: return "throw_catch";
    case game::RhinoBossTaskState::ThrowStruggle: return "throw_struggle";
    case game::RhinoBossTaskState::ThrowSuccess: return "throw_success";
    case game::RhinoBossTaskState::ThrowRelease: return "throw_release";
    case game::RhinoBossTaskState::ThrowMiss: return "throw_miss";
    }
    return "unknown";
}

std::string AutoplayHarness::robotPhantomTaskName(
    game::RobotPhantomTaskState task) {
    switch (task) {
    case game::RobotPhantomTaskState::None: return "none";
    case game::RobotPhantomTaskState::ApproachMelee:
        return "approach_melee";
    case game::RobotPhantomTaskState::RushReady: return "rush_ready";
    case game::RobotPhantomTaskState::RushFirst: return "rush_first";
    case game::RobotPhantomTaskState::RushSecond: return "rush_second";
    case game::RobotPhantomTaskState::RushRecovery:
        return "rush_recovery";
    case game::RobotPhantomTaskState::ConcealReady:
        return "conceal_ready";
    case game::RobotPhantomTaskState::ConcealHidden:
        return "conceal_hidden";
    case game::RobotPhantomTaskState::ConcealAttack:
        return "conceal_attack";
    case game::RobotPhantomTaskState::ConcealRecovery:
        return "conceal_recovery";
    case game::RobotPhantomTaskState::ApproachRange:
        return "approach_range";
    case game::RobotPhantomTaskState::ThrowReady: return "throw_ready";
    case game::RobotPhantomTaskState::Throw: return "throw";
    case game::RobotPhantomTaskState::ThrowWait: return "throw_wait";
    case game::RobotPhantomTaskState::ThrowRecovery:
        return "throw_recovery";
    }
    return "unknown";
}

std::string AutoplayHarness::electroTaskName(
    game::ElectroBossTaskState task) {
    switch (task) {
    case game::ElectroBossTaskState::None: return "none";
    case game::ElectroBossTaskState::RangeAttack: return "range_attack";
    case game::ElectroBossTaskState::WeakStart: return "weak_start";
    case game::ElectroBossTaskState::Weak: return "weak";
    case game::ElectroBossTaskState::WeakEnd: return "weak_end";
    case game::ElectroBossTaskState::RotateReady: return "rotate_ready";
    case game::ElectroBossTaskState::Rotate: return "rotate";
    case game::ElectroBossTaskState::RotateEnd: return "rotate_end";
    case game::ElectroBossTaskState::DashReady: return "dash_ready";
    case game::ElectroBossTaskState::DashRush: return "dash_rush";
    case game::ElectroBossTaskState::DashLand: return "dash_land";
    case game::ElectroBossTaskState::DashEnd: return "dash_end";
    }
    return "unknown";
}

std::string AutoplayHarness::csv(std::string_view value) {
    std::string result{"\""};
    for (const char character : value) {
        if (character == '"') {
            result += "\"\"";
        } else if (character == '\r' || character == '\n') {
            result.push_back(' ');
        } else {
            result.push_back(character);
        }
    }
    result.push_back('"');
    return result;
}

} // namespace usm::diagnostics
