/**
 * @file physics_scene.cpp
 * @brief 物理场景管理器实现 - 基于 Jolt Physics
 * 
 * 参考 Piccolo 项目实现
 */
#include "physics_scene.h"
#include "../core/base/macro.h"
#include "../core/log/log_system.h"
#include <cstdarg>
#include <cstdio>

JPH_SUPPRESS_WARNINGS

namespace Elish
{
    static void TraceImpl(const char* inFMT, ...)
    {
        va_list list;
        va_start(list, inFMT);
        char buffer[1024];
        vsnprintf(buffer, sizeof(buffer), inFMT, list);
        va_end(list);
        LOG_INFO("[Jolt] {}", buffer);
    }

#ifdef JPH_ENABLE_ASSERTS
    static bool AssertFailedImpl(const char* inExpression, const char* inMessage, const char* inFile, JPH::uint inLine)
    {
        LOG_ERROR("[Jolt Assert] {}:{}: ({}) {}", inFile, inLine, inExpression, inMessage ? inMessage : "");
        return true;
    }
#endif
    namespace
    {
        namespace Layers
        {
            static constexpr JPH::ObjectLayer NON_MOVING = 0;
            static constexpr JPH::ObjectLayer MOVING = 1;
            static constexpr JPH::ObjectLayer NUM_LAYERS = 2;
        }

        namespace BroadPhaseLayers
        {
            static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
            static constexpr JPH::BroadPhaseLayer MOVING(1);
            static constexpr uint32_t NUM_LAYERS(2);
        }

        class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface
        {
        public:
            BPLayerInterfaceImpl()
            {
                m_object_to_broad_phase[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
                m_object_to_broad_phase[Layers::MOVING] = BroadPhaseLayers::MOVING;
            }

            virtual uint32_t GetNumBroadPhaseLayers() const override
            {
                return BroadPhaseLayers::NUM_LAYERS;
            }

            virtual JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override
            {
                JPH_ASSERT(inLayer < Layers::NUM_LAYERS);
                return m_object_to_broad_phase[inLayer];
            }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
            virtual const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override
            {
                switch ((JPH::BroadPhaseLayer::Type)inLayer)
                {
                case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::NON_MOVING: return "NON_MOVING";
                case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::MOVING: return "MOVING";
                default: JPH_ASSERT(false); return "INVALID";
                }
            }
#endif

        private:
            JPH::BroadPhaseLayer m_object_to_broad_phase[Layers::NUM_LAYERS];
        };

        class ObjectLayerPairFilterImpl : public JPH::ObjectLayerPairFilter
        {
        public:
            virtual bool ShouldCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const override
            {
                switch (inObject1)
                {
                case Layers::NON_MOVING:
                    return inObject2 == Layers::MOVING;
                case Layers::MOVING:
                    return true;
                default:
                    JPH_ASSERT(false);
                    return false;
                }
            }
        };

        class ObjectVsBroadPhaseLayerFilterImpl : public JPH::ObjectVsBroadPhaseLayerFilter
        {
        public:
            virtual bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override
            {
                switch (inLayer1)
                {
                case Layers::NON_MOVING:
                    return inLayer2 == BroadPhaseLayers::MOVING;
                case Layers::MOVING:
                    return true;
                default:
                    JPH_ASSERT(false);
                    return false;
                }
            }
        };

        class MyContactListener : public JPH::ContactListener
        {
        public:
            virtual JPH::ValidateResult OnContactValidate(const JPH::Body& inBody1, const JPH::Body& inBody2,
                                                          JPH::RVec3Arg inBaseOffset, const JPH::CollideShapeResult& inCollisionResult) override
            {
                return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
            }

            virtual void OnContactAdded(const JPH::Body& inBody1, const JPH::Body& inBody2,
                                        const JPH::ContactManifold& inManifold, JPH::ContactSettings& ioSettings) override {}

            virtual void OnContactPersisted(const JPH::Body& inBody1, const JPH::Body& inBody2,
                                            const JPH::ContactManifold& inManifold, JPH::ContactSettings& ioSettings) override {}

            virtual void OnContactRemoved(const JPH::SubShapeIDPair& inSubShapePair) override {}
        };

        class MyBodyActivationListener : public JPH::BodyActivationListener
        {
        public:
            virtual void OnBodyActivated(const JPH::BodyID& inBodyID, JPH::uint64 inBodyUserData) override {}

            virtual void OnBodyDeactivated(const JPH::BodyID& inBodyID, JPH::uint64 inBodyUserData) override {}
        };
    }

    PhysicsScene::PhysicsScene()
    {
    }

    PhysicsScene::~PhysicsScene()
    {
        clear();
    }

    bool PhysicsScene::initialize()
    {
        if (m_initialized)
        {
            return true;
        }

        LOG_INFO("[Jolt] Initializing Jolt Physics...");

        JPH::RegisterDefaultAllocator();
        JPH::Trace = TraceImpl;
        JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = AssertFailedImpl;)

        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();

        m_temp_allocator = new JPH::TempAllocatorImpl(10 * 1024 * 1024);

        uint32_t num_threads = std::max(1u, std::thread::hardware_concurrency() - 1);
        m_job_system = new JPH::JobSystemThreadPool(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, num_threads);

        m_broad_phase_layer_interface = new BPLayerInterfaceImpl();
        m_object_layer_pair_filter = new ObjectLayerPairFilterImpl();
        m_object_vs_broad_phase_layer_filter = new ObjectVsBroadPhaseLayerFilterImpl();

        const JPH::uint cMaxBodies = 65536;
        const JPH::uint cNumBodyMutexes = 0;
        const JPH::uint cMaxBodyPairs = 65536;
        const JPH::uint cMaxContactConstraints = 10240;

        m_physics_system = new JPH::PhysicsSystem();
        m_physics_system->Init(cMaxBodies, cNumBodyMutexes, cMaxBodyPairs, cMaxContactConstraints,
                               *m_broad_phase_layer_interface,
                               *m_object_vs_broad_phase_layer_filter,
                               *m_object_layer_pair_filter);

        m_physics_system->SetBodyActivationListener(new MyBodyActivationListener());
        m_physics_system->SetContactListener(new MyContactListener());

#ifdef JPH_DEBUG_RENDERER
        m_debug_renderer = std::make_unique<JoltDebugRenderer>();
        LOG_INFO("[Jolt] Debug renderer initialized");
#endif

        m_initialized = true;
        LOG_INFO("[Jolt] Jolt Physics initialized successfully");
        return true;
    }

    void PhysicsScene::clear()
    {
        if (!m_initialized)
        {
            return;
        }

        if (m_physics_system)
        {
            delete m_physics_system;
            m_physics_system = nullptr;
        }

        if (m_object_vs_broad_phase_layer_filter)
        {
            delete m_object_vs_broad_phase_layer_filter;
            m_object_vs_broad_phase_layer_filter = nullptr;
        }

        if (m_object_layer_pair_filter)
        {
            delete m_object_layer_pair_filter;
            m_object_layer_pair_filter = nullptr;
        }

        if (m_broad_phase_layer_interface)
        {
            delete m_broad_phase_layer_interface;
            m_broad_phase_layer_interface = nullptr;
        }

        if (m_job_system)
        {
            delete m_job_system;
            m_job_system = nullptr;
        }

        if (m_temp_allocator)
        {
            delete m_temp_allocator;
            m_temp_allocator = nullptr;
        }

        if (JPH::Factory::sInstance)
        {
            delete JPH::Factory::sInstance;
            JPH::Factory::sInstance = nullptr;
        }

        m_body_names.clear();
        m_body_user_data.clear();

        m_initialized = false;
    }

    void PhysicsScene::update(float delta_time)
    {
        if (!m_initialized || !m_physics_system)
        {
            return;
        }

        m_physics_system->Update(delta_time, 1, m_temp_allocator, m_job_system);
    }

    JPH::BodyID PhysicsScene::createBoxBody(const RigidBodyCreateInfo& create_info)
    {
        if (!m_initialized || !m_physics_system)
        {
            return JPH::BodyID();
        }

        JPH::ShapeRefC shape;
        
        if (!create_info.m_vertices.empty())
        {
            std::vector<JPH::Vec3> jolt_vertices;
            jolt_vertices.reserve(create_info.m_vertices.size());
            for (const auto& v : create_info.m_vertices)
            {
                jolt_vertices.push_back(toJolt(v));
            }
            
            JPH::ConvexHullShapeSettings hull_settings(jolt_vertices.data(), 
                                                        static_cast<int>(jolt_vertices.size()));
            JPH::ShapeSettings::ShapeResult hull_result = hull_settings.Create();
            if (hull_result.HasError())
            {
                LOG_ERROR("[Jolt] Failed to create convex hull shape: {}", hull_result.GetError().c_str());
                return JPH::BodyID();
            }
            shape = hull_result.Get();
        }
        else
        {
            JPH::BoxShapeSettings box_shape(toJolt(create_info.m_half_extent));
            JPH::ShapeSettings::ShapeResult box_result = box_shape.Create();
            if (box_result.HasError())
            {
                LOG_ERROR("[Jolt] Failed to create box shape: {}", box_result.GetError().c_str());
                return JPH::BodyID();
            }
            shape = box_result.Get();
        }

        JPH::ShapeRefC final_shape = shape;
        if (create_info.m_scale != glm::vec3(1.0f))
        {
            JPH::ScaledShapeSettings scaled_settings(shape, toJolt(create_info.m_scale));
            JPH::ShapeSettings::ShapeResult scaled_result = scaled_settings.Create();
            if (scaled_result.HasError())
            {
                LOG_ERROR("[Jolt] Failed to create scaled shape: {}", scaled_result.GetError().c_str());
                return JPH::BodyID();
            }
            final_shape = scaled_result.Get();
        }

        JPH::ObjectLayer layer = create_info.m_is_dynamic ? Layers::MOVING : Layers::NON_MOVING;

        JPH::BodyCreationSettings body_settings(
            final_shape,
            JPH::RVec3(toJolt(create_info.m_position)),
            toJolt(create_info.m_rotation),
            create_info.m_is_dynamic ? JPH::EMotionType::Dynamic : JPH::EMotionType::Static,
            layer
        );

        if (create_info.m_is_dynamic)
        {
            body_settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateMassAndInertia;
            body_settings.mMassPropertiesOverride.mMass = 1.0f;
        }

        JPH::BodyInterface& body_interface = m_physics_system->GetBodyInterface();
        JPH::Body* body = body_interface.CreateBody(body_settings);
        if (!body)
        {
            LOG_ERROR("[Jolt] Failed to create body");
            return JPH::BodyID();
        }

        body->SetUserData(reinterpret_cast<JPH::uint64>(create_info.m_user_data));
        body_interface.AddBody(body->GetID(), JPH::EActivation::DontActivate);

        uint32_t id = body->GetID().GetIndexAndSequenceNumber();
        m_body_names[id] = create_info.m_name;
        if (create_info.m_user_data)
        {
            m_body_user_data[id] = create_info.m_user_data;
        }

        LOG_INFO("[Jolt] Created body '{}' with ID {} (vertices: {}, scale: {:.2f},{:.2f},{:.2f})", 
                 create_info.m_name, id, create_info.m_vertices.size(),
                 create_info.m_scale.x, create_info.m_scale.y, create_info.m_scale.z);
        return body->GetID();
    }

    void PhysicsScene::removeBody(JPH::BodyID body_id)
    {
        if (!m_initialized || !m_physics_system || body_id.IsInvalid())
        {
            return;
        }

        JPH::BodyInterface& body_interface = m_physics_system->GetBodyInterface();
        body_interface.RemoveBody(body_id);
        body_interface.DestroyBody(body_id);

        uint32_t id = body_id.GetIndexAndSequenceNumber();
        m_body_names.erase(id);
        m_body_user_data.erase(id);
    }

    void PhysicsScene::setBodyPosition(JPH::BodyID body_id, const glm::vec3& position)
    {
        if (!m_initialized || !m_physics_system || body_id.IsInvalid())
        {
            return;
        }

        JPH::BodyInterface& body_interface = m_physics_system->GetBodyInterface();
        body_interface.SetPosition(body_id, JPH::RVec3(toJolt(position)), JPH::EActivation::DontActivate);
    }

    void PhysicsScene::setBodyRotation(JPH::BodyID body_id, const glm::quat& rotation)
    {
        if (!m_initialized || !m_physics_system || body_id.IsInvalid())
        {
            return;
        }

        JPH::BodyInterface& body_interface = m_physics_system->GetBodyInterface();
        body_interface.SetRotation(body_id, toJolt(rotation), JPH::EActivation::DontActivate);
    }

    void PhysicsScene::setBodyScale(JPH::BodyID body_id, const glm::vec3& scale)
     {
         if (!m_initialized || !m_physics_system || body_id.IsInvalid())
         {
             return;
         }

         JPH::BodyInterface& body_interface = m_physics_system->GetBodyInterface();
         
         JPH::BodyLockRead lock_read(m_physics_system->GetBodyLockInterface(), body_id);
         if (lock_read.Succeeded())
         {
             const JPH::Body& body = lock_read.GetBody();
             JPH::ShapeRefC current_shape = body.GetShape();
             
             JPH::ScaledShapeSettings scaled_settings(current_shape, toJolt(scale));
             JPH::ShapeSettings::ShapeResult result = scaled_settings.Create();
             
             if (result.IsValid())
              {
                  lock_read.ReleaseLock();
                  body_interface.SetShape(body_id, result.Get(), false, JPH::EActivation::DontActivate);
              }
         }
     }

    void PhysicsScene::setBodyTransform(JPH::BodyID body_id, const glm::vec3& position, 
                                        const glm::quat& rotation, const glm::vec3& scale)
    {
        if (!m_initialized || !m_physics_system || body_id.IsInvalid())
        {
            return;
        }

        glm::quat normalized_rotation = glm::normalize(rotation);
        
        glm::vec3 safe_scale = scale;
        safe_scale = glm::max(safe_scale, glm::vec3(0.001f));

        JPH::BodyInterface& body_interface = m_physics_system->GetBodyInterface();
        
        body_interface.SetPositionAndRotation(body_id, JPH::RVec3(toJolt(position)), 
                                               toJolt(normalized_rotation), JPH::EActivation::DontActivate);
        
        setBodyScale(body_id, safe_scale);
    }

    void PhysicsScene::updateBodyVertices(JPH::BodyID body_id, const std::vector<glm::vec3>& vertices)
    {
        if (!m_initialized || !m_physics_system || body_id.IsInvalid() || vertices.empty())
        {
            return;
        }

        std::vector<JPH::Vec3> jolt_vertices;
        jolt_vertices.reserve(vertices.size());
        for (const auto& v : vertices)
        {
            jolt_vertices.push_back(toJolt(v));
        }

        JPH::ConvexHullShapeSettings hull_settings(jolt_vertices.data(), 
                                                    static_cast<int>(jolt_vertices.size()));
        JPH::ShapeSettings::ShapeResult hull_result = hull_settings.Create();
        
        if (!hull_result.IsValid())
        {
            LOG_ERROR("[Jolt] Failed to create convex hull for body update: {}", 
                      hull_result.GetError().c_str());
            return;
        }

        JPH::BodyInterface& body_interface = m_physics_system->GetBodyInterface();
        body_interface.SetShape(body_id, hull_result.Get(), false, JPH::EActivation::DontActivate);
    }

    glm::vec3 PhysicsScene::getBodyPosition(JPH::BodyID body_id) const
    {
        if (!m_initialized || !m_physics_system || body_id.IsInvalid())
        {
            return glm::vec3(0.0f);
        }

        JPH::BodyInterface& body_interface = m_physics_system->GetBodyInterface();
        return toGLM(JPH::Vec3(body_interface.GetPosition(body_id)));
    }

    glm::quat PhysicsScene::getBodyRotation(JPH::BodyID body_id) const
    {
        if (!m_initialized || !m_physics_system || body_id.IsInvalid())
        {
            return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        }

        JPH::BodyInterface& body_interface = m_physics_system->GetBodyInterface();
        return toGLM(body_interface.GetRotation(body_id));
    }

    bool PhysicsScene::raycast(const glm::vec3& ray_origin,
                               const glm::vec3& ray_direction,
                               float ray_length,
                               RayCastResult& out_result)
    {
        if (!m_initialized || !m_physics_system)
        {
            return false;
        }

        JPH::RRayCast ray;
        ray.mOrigin = JPH::RVec3(toJolt(ray_origin));
        ray.mDirection = toJolt(glm::normalize(ray_direction) * ray_length);

        JPH::RayCastResult result;
        const JPH::NarrowPhaseQuery& narrow_phase = m_physics_system->GetNarrowPhaseQuery();
        
        if (narrow_phase.CastRay(ray, result))
        {
            JPH::BodyLockRead lock(m_physics_system->GetBodyLockInterface(), result.mBodyID);
            if (lock.Succeeded())
            {
                const JPH::Body& body = lock.GetBody();

                out_result.m_hit = true;
                out_result.m_body_id = body.GetID();
                out_result.m_hit_distance = result.mFraction * ray_length;
                out_result.m_hit_position = ray_origin + glm::normalize(ray_direction) * out_result.m_hit_distance;
                out_result.m_hit_normal = toGLM(body.GetWorldSpaceSurfaceNormal(result.mSubShapeID2, ray.GetPointOnRay(result.mFraction)));

                uint32_t id = body.GetID().GetIndexAndSequenceNumber();
                out_result.m_body_name = getBodyName(body.GetID());
                
                auto it = m_body_user_data.find(id);
                if (it != m_body_user_data.end())
                {
                    out_result.m_user_data = it->second;
                }

                return true;
            }
        }

        out_result.m_hit = false;
        return false;
    }

    bool PhysicsScene::raycastAll(const glm::vec3& ray_origin,
                                  const glm::vec3& ray_direction,
                                  float ray_length,
                                  std::vector<RayCastResult>& out_results)
    {
        if (!m_initialized || !m_physics_system)
        {
            return false;
        }

        JPH::RayCast ray;
        ray.mOrigin = toJolt(ray_origin);
        ray.mDirection = toJolt(glm::normalize(ray_direction) * ray_length);

        JPH::AllHitCollisionCollector<JPH::RayCastBodyCollector> collector;
        const JPH::BroadPhaseQuery& broad_phase = m_physics_system->GetBroadPhaseQuery();
        broad_phase.CastRay(ray, collector);

        if (collector.HadHit())
        {
            for (const auto& hit : collector.mHits)
            {
                RayCastResult result;
                result.m_hit = true;
                result.m_body_id = hit.mBodyID;
                result.m_hit_distance = hit.mFraction * ray_length;
                result.m_hit_position = ray_origin + glm::normalize(ray_direction) * result.m_hit_distance;
                result.m_body_name = getBodyName(hit.mBodyID);

                uint32_t id = hit.mBodyID.GetIndexAndSequenceNumber();
                auto it = m_body_user_data.find(id);
                if (it != m_body_user_data.end())
                {
                    result.m_user_data = it->second;
                }

                out_results.push_back(result);
            }
            return true;
        }

        return false;
    }

    void PhysicsScene::setBodyName(JPH::BodyID body_id, const std::string& name)
    {
        uint32_t id = body_id.GetIndexAndSequenceNumber();
        m_body_names[id] = name;
    }

    std::string PhysicsScene::getBodyName(JPH::BodyID body_id) const
    {
        uint32_t id = body_id.GetIndexAndSequenceNumber();
        auto it = m_body_names.find(id);
        if (it != m_body_names.end())
        {
            return it->second;
        }
        return "Unknown";
    }

    JPH::BodyID PhysicsScene::getBodyIDByName(const std::string& name) const
    {
        for (const auto& pair : m_body_names)
        {
            if (pair.second == name)
            {
                return JPH::BodyID(pair.first);
            }
        }
        return JPH::BodyID();
    }

    void PhysicsScene::drawDebug()
    {
        if (!m_initialized || !m_physics_system || !m_debug_draw_callback)
        {
            return;
        }

        JPH::BodyIDVector body_ids;
        m_physics_system->GetBodies(body_ids);

        for (const JPH::BodyID& body_id : body_ids)
        {
            JPH::BodyLockRead lock(m_physics_system->GetBodyLockInterface(), body_id);
            if (lock.Succeeded())
            {
                const JPH::Body& body = lock.GetBody();
                
                JPH::RVec3 position = body.GetCenterOfMassPosition();
                JPH::Quat rotation = body.GetRotation();
                
                const JPH::Shape* shape = body.GetShape();
                JPH::AABox local_bounds = shape->GetLocalBounds();
                
                JPH::Mat44 transform = JPH::Mat44::sRotationTranslation(rotation, JPH::Vec3(position));
                
                JPH::Vec3 half_extent = local_bounds.GetSize() * 0.5f;
                
                JPH::Vec3 corners[8];
                corners[0] = transform * JPH::Vec3(-half_extent.GetX(), -half_extent.GetY(), -half_extent.GetZ());
                corners[1] = transform * JPH::Vec3( half_extent.GetX(), -half_extent.GetY(), -half_extent.GetZ());
                corners[2] = transform * JPH::Vec3( half_extent.GetX(),  half_extent.GetY(), -half_extent.GetZ());
                corners[3] = transform * JPH::Vec3(-half_extent.GetX(),  half_extent.GetY(), -half_extent.GetZ());
                corners[4] = transform * JPH::Vec3(-half_extent.GetX(), -half_extent.GetY(),  half_extent.GetZ());
                corners[5] = transform * JPH::Vec3( half_extent.GetX(), -half_extent.GetY(),  half_extent.GetZ());
                corners[6] = transform * JPH::Vec3( half_extent.GetX(),  half_extent.GetY(),  half_extent.GetZ());
                corners[7] = transform * JPH::Vec3(-half_extent.GetX(),  half_extent.GetY(),  half_extent.GetZ());
                
                glm::vec3 min_pos(FLT_MAX);
                glm::vec3 max_pos(-FLT_MAX);
                for (int i = 0; i < 8; ++i)
                {
                    glm::vec3 corner = toGLM(corners[i]);
                    min_pos = glm::min(min_pos, corner);
                    max_pos = glm::max(max_pos, corner);
                }
                glm::vec3 center = (min_pos + max_pos) * 0.5f;
                
                m_debug_draw_callback(min_pos, max_pos, center);
            }
        }
    }

#ifdef JPH_DEBUG_RENDERER
    void PhysicsScene::drawPhysicsBodies(const glm::mat4& view_matrix, const glm::mat4& proj_matrix,
                                          float viewport_width, float viewport_height)
    {
        if (!m_initialized || !m_physics_system || !m_debug_renderer)
        {
            return;
        }

        m_debug_renderer->setViewProjection(view_matrix, proj_matrix, viewport_width, viewport_height);
        m_debug_renderer->clearDebugData();

        JPH::BodyManager::DrawSettings draw_settings;
        draw_settings.mDrawShape = true;
        draw_settings.mDrawShapeWireframe = true;
        draw_settings.mDrawGetSupportFunction = false;
        draw_settings.mDrawSupportDirection = false;
        draw_settings.mDrawGetSupportingFace = false;
        draw_settings.mDrawVelocity = false;
        draw_settings.mDrawMassAndInertia = false;
        draw_settings.mDrawSleepStats = false;
        draw_settings.mDrawBoundingBox = false;
        draw_settings.mDrawCenterOfMassTransform = false;
        draw_settings.mDrawWorldTransform = false;

        JPH::BodyDrawFilter draw_filter;

        m_physics_system->DrawBodies(draw_settings, m_debug_renderer.get(), &draw_filter);

        // LOG_DEBUG("[Jolt] Drew {} debug lines", m_debug_renderer->getLineCount());
    }

    size_t PhysicsScene::getDebugLineCount() const
    {
        return m_debug_renderer ? m_debug_renderer->getLineCount() : 0;
    }
#endif

    JPH::Vec3 PhysicsScene::toJolt(const glm::vec3& v)
    {
        return JPH::Vec3(v.x, v.y, v.z);
    }

    glm::vec3 PhysicsScene::toGLM(const JPH::Vec3& v)
    {
        return glm::vec3(v.GetX(), v.GetY(), v.GetZ());
    }

    JPH::Quat PhysicsScene::toJolt(const glm::quat& q)
    {
        return JPH::Quat(q.x, q.y, q.z, q.w);
    }

    glm::quat PhysicsScene::toGLM(const JPH::Quat& q)
    {
        return glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ());
    }
}
