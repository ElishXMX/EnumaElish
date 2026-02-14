#include "asset_browser_ui.h"
#include "../core/base/macro.h"
#include "../render/interface/rhi.h"
#include "../render/interface/vulkan/vulkan_rhi.h"
#include "../render/interface/vulkan/vulkan_rhi_resource.h"
#include "../global/global_context.h"
#include "../render/render_system.h"

#include <algorithm>
#include <filesystem>

namespace Elish
{
    bool AssetBrowserUI::initialize(std::shared_ptr<RHI> rhi)
    {
        if (m_initialized)
        {
            return true;
        }
        
        if (!rhi)
        {
            LOG_ERROR("[AssetBrowserUI] RHI is null, cannot initialize");
            return false;
        }
        
        m_rhi = rhi;
        
        // 扫描资产
        AssetManager::getInstance().scanAssets();
        
        m_initialized = true;
        m_needsRefresh = true;
        
        LOG_INFO("[AssetBrowserUI] Initialized successfully");
        return true;
    }
    
    void AssetBrowserUI::cleanup()
    {
        // 清理缩略图缓存
        for (auto& pair : m_thumbnailCache)
        {
            // 注意：RHI资源由RHI管理，这里不需要手动释放
            // 如果需要手动释放，应该调用RHI的销毁方法
        }
        m_thumbnailCache.clear();
        
        m_initialized = false;
    }
    
    void AssetBrowserUI::render()
    {
        if (!m_initialized || !m_visible)
        {
            return;
        }
        
        // 渲染搜索栏
        renderSearchBar();
        
        ImGui::SameLine();
        
        // 渲染类型过滤器
        renderTypeFilter();
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        
        // 获取要显示的资产列表
        if (m_needsRefresh)
        {
            refreshAssets();
            m_needsRefresh = false;
        }
        
        // 渲染资产网格
        renderAssetGrid(m_currentDisplayAssets);
        
        // 如果有选中的资产，渲染详情
        if (m_selectedAsset)
        {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            renderAssetDetails();
        }
    }
    
    void AssetBrowserUI::renderSearchBar()
    {
        ImGui::Text("🔍 ");
        ImGui::SameLine();
        
        ImGui::PushItemWidth(200);
        
        // 使用静态缓冲区来避免 std::string 与 char* 的转换问题
        static char searchBuffer[256] = "";
        if (ImGui::InputText("##AssetSearch", searchBuffer, sizeof(searchBuffer), ImGuiInputTextFlags_EnterReturnsTrue))
        {
            m_searchQuery = searchBuffer;
            m_needsRefresh = true;
        }
        
        // 同步搜索缓冲区
        if (m_searchQuery != searchBuffer)
        {
            strncpy(searchBuffer, m_searchQuery.c_str(), sizeof(searchBuffer) - 1);
            searchBuffer[sizeof(searchBuffer) - 1] = '\0';
        }
        
        // 清除按钮
        if (strlen(searchBuffer) > 0)
        {
            ImGui::SameLine();
            if (ImGui::Button("X"))
            {
                searchBuffer[0] = '\0';
                m_searchQuery.clear();
                m_needsRefresh = true;
            }
        }
        ImGui::PopItemWidth();
        
        // 实时搜索
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            m_searchQuery = searchBuffer;
            m_needsRefresh = true;
        }
    }
    
    void AssetBrowserUI::renderTypeFilter()
    {
        const char* typeNames[] = {
            "All",
            "Models",
            "Textures", 
            "Materials",
            "Shaders",
            "Levels"
        };
        
        ImGui::Text("Filter: ");
        ImGui::SameLine();
        
        for (int i = 0; i < 6; ++i)
        {
            if (i > 0) ImGui::SameLine();
            
            bool isSelected = (m_selectedTypeFilter == i);
            if (ImGui::Checkbox(typeNames[i], &isSelected))
            {
                if (isSelected)
                {
                    m_selectedTypeFilter = i;
                    m_needsRefresh = true;
                }
            }
        }
    }
    
    void AssetBrowserUI::renderAssetGrid(const std::vector<AssetInfo>& assets)
    {
        if (assets.empty())
        {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No assets found");
            return;
        }
        
        // 计算网格布局
        float windowVisibleX = ImGui::GetContentRegionAvail().x;
        float itemSize = m_thumbnailSize * m_zoomLevel;
        float itemSpacing = 10.0f;
        int columns = std::max(1, static_cast<int>(windowVisibleX / (itemSize + itemSpacing)));
        
        // 缩放控制
        ImGui::Text("Zoom: ");
        ImGui::SameLine();
        ImGui::PushItemWidth(100);
        ImGui::SliderFloat("##Zoom", &m_zoomLevel, 0.5f, 2.0f, "%.1fx");
        ImGui::PopItemWidth();
        
        ImGui::Spacing();
        
        // 开始网格布局
        ImGui::BeginChild("AssetGrid", ImVec2(0, 0), true);
        
        int columnCounter = 0;
        for (const auto& asset : assets)
        {
            ImGui::PushID(asset.path.c_str());
            
            if (columnCounter > 0)
            {
                ImGui::SameLine(0.0f, itemSpacing);
            }
            
            // 渲染资产项
            bool wasSelected = (m_selectedAssetPath == asset.path);
            renderAssetItem(asset, itemSize);
            
            columnCounter++;
            if (columnCounter >= columns)
            {
                columnCounter = 0;
            }
            
            ImGui::PopID();
        }
        
        ImGui::EndChild();
    }
    
    bool AssetBrowserUI::renderAssetItem(const AssetInfo& asset, float itemSize)
    {
        bool isSelected = (m_selectedAssetPath == asset.path);
        
        // 选中高亮
        if (isSelected)
        {
            ImVec4 selectedColor = ImVec4(0.2f, 0.4f, 0.8f, 0.5f);
            ImVec2 pos = ImGui::GetCursorScreenPos();
            ImVec2 size = ImVec2(itemSize, itemSize + 25.0f);
            ImGui::GetWindowDrawList()->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), 
                ImGui::GetColorU32(selectedColor), 5.0f);
        }
        
        ImGui::BeginGroup();
        
        // 渲染缩略图/图标
        ImVec2 thumbnailSize = ImVec2(itemSize, itemSize * 0.8f);
        renderThumbnail(asset, thumbnailSize);
        
        // 渲染文件名
        ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + itemSize);
        ImGui::Text("%s", asset.name.c_str());
        ImGui::PopTextWrapPos();
        
        // 显示加载状态
        if (asset.isLoaded)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "*");
        }
        
        ImGui::EndGroup();
        
        // 处理点击
        if (ImGui::IsItemClicked())
        {
            handleAssetSelection(asset);
            isSelected = true;
        }
        
        // 处理双击
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
        {
            handleAssetDoubleClick(asset);
        }
        
        // 悬停提示
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("%s\nType: %s\nSize: %s", 
                asset.name.c_str(), 
                asset.getTypeDisplayName().c_str(),
                asset.fileSizeFormatted.c_str());
        }
        
        return isSelected;
    }
    
    void AssetBrowserUI::renderThumbnail(const AssetInfo& asset, ImVec2 size)
    {
        // 对于纹理类型，尝试加载并显示缩略图
        if (asset.type == AssetType::Texture)
        {
            ImTextureID textureId = getOrCreateTextureThumbnail(asset);
            if (textureId)
            {
                ImGui::Image(textureId, size);
                return;
            }
        }
        
        // 其他类型或加载失败，显示默认图标
        renderDefaultIcon(asset.type, size);
    }
    
    ImTextureID AssetBrowserUI::getOrCreateTextureThumbnail(const AssetInfo& asset)
    {
        // 检查缓存
        auto it = m_thumbnailCache.find(asset.absolutePath);
        if (it != m_thumbnailCache.end() && it->second.loaded)
        {
            return it->second.textureId;
        }
        
        // 尝试加载纹理缩略图
        if (loadTextureThumbnail(asset.absolutePath))
        {
            it = m_thumbnailCache.find(asset.absolutePath);
            if (it != m_thumbnailCache.end())
            {
                return it->second.textureId;
            }
        }
        
        return nullptr;
    }
    
    bool AssetBrowserUI::loadTextureThumbnail(const std::string& path)
    {
        // 当前ImGui版本不支持动态添加纹理，暂时跳过真实缩略图加载
        // 后续可以通过扩展ImGui Vulkan后端来支持
        (void)path;
        return false;
    }
    
    void AssetBrowserUI::renderDefaultIcon(AssetType type, ImVec2 size)
    {
        // 使用不同颜色区分类型
        ImVec4 bgColor;
        const char* icon = "";
        
        switch (type)
        {
            case AssetType::Model:
                bgColor = ImVec4(0.3f, 0.5f, 0.7f, 1.0f);
                icon = "📦";
                break;
            case AssetType::Texture:
                bgColor = ImVec4(0.5f, 0.7f, 0.3f, 1.0f);
                icon = "🖼️";
                break;
            case AssetType::Material:
                bgColor = ImVec4(0.7f, 0.5f, 0.7f, 1.0f);
                icon = "🎨";
                break;
            case AssetType::Shader:
                bgColor = ImVec4(0.7f, 0.7f, 0.3f, 1.0f);
                icon = "✨";
                break;
            case AssetType::Level:
                bgColor = ImVec4(0.5f, 0.5f, 0.7f, 1.0f);
                icon = "🗺️";
                break;
            default:
                bgColor = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
                icon = "📄";
                break;
        }
        
        // 绘制背景
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled(
            pos, 
            ImVec2(pos.x + size.x, pos.y + size.y), 
            ImGui::GetColorU32(bgColor),
            5.0f
        );
        
        // 绘制图标
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + size.y * 0.3f);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + size.x * 0.3f);
        ImGui::Text("%s", icon);
        
        // 占位
        ImGui::SetCursorPosY(pos.y - ImGui::GetWindowPos().y + ImGui::GetScrollY());
        ImGui::Dummy(size);
    }
    
    void AssetBrowserUI::renderAssetDetails()
    {
        if (!m_selectedAsset)
        {
            return;
        }
        
        ImGui::BeginChild("AssetDetails", ImVec2(0, 100), true);
        
        ImGui::Text("Asset Details:");
        ImGui::Separator();
        
        ImGui::Columns(2, "asset_details_cols", false);
        ImGui::SetColumnWidth(0, 100);
        
        ImGui::Text("Name:"); ImGui::NextColumn();
        ImGui::Text("%s", m_selectedAsset->name.c_str()); ImGui::NextColumn();
        
        ImGui::Text("Type:"); ImGui::NextColumn();
        ImGui::Text("%s", m_selectedAsset->getTypeDisplayName().c_str()); ImGui::NextColumn();
        
        ImGui::Text("Size:"); ImGui::NextColumn();
        ImGui::Text("%s", m_selectedAsset->fileSizeFormatted.c_str()); ImGui::NextColumn();
        
        ImGui::Text("Path:"); ImGui::NextColumn();
        ImGui::Text("%s", m_selectedAsset->path.c_str()); ImGui::NextColumn();
        
        ImGui::Text("Status:"); ImGui::NextColumn();
        if (m_selectedAsset->isLoaded)
        {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Loaded");
        }
        else
        {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Not Loaded");
        }
        
        ImGui::Columns(1);
        
        ImGui::EndChild();
    }
    
    void AssetBrowserUI::refreshAssets()
    {
        // 确保资产已扫描
        AssetManager& assetManager = AssetManager::getInstance();
        if (!assetManager.isAssetsScanned())
        {
            assetManager.scanAssets();
        }
        
        // 根据过滤条件获取资产
        m_currentDisplayAssets.clear();
        
        AssetType typeFilter = AssetType::Unknown;
        switch (m_selectedTypeFilter)
        {
            case 1: typeFilter = AssetType::Model; break;
            case 2: typeFilter = AssetType::Texture; break;
            case 3: typeFilter = AssetType::Material; break;
            case 4: typeFilter = AssetType::Shader; break;
            case 5: typeFilter = AssetType::Level; break;
            default: typeFilter = AssetType::Unknown; break;
        }
        
        if (m_searchQuery.empty())
        {
            if (typeFilter == AssetType::Unknown)
            {
                m_currentDisplayAssets = assetManager.getAllAssets();
            }
            else
            {
                m_currentDisplayAssets = assetManager.getAssetsByType(typeFilter);
            }
        }
        else
        {
            m_currentDisplayAssets = assetManager.searchAssets(m_searchQuery, typeFilter);
        }
    }
    
    void AssetBrowserUI::handleAssetSelection(const AssetInfo& asset)
    {
        m_selectedAssetPath = asset.path;
        m_selectedAsset = &asset;
    }
    
    void AssetBrowserUI::handleAssetDoubleClick(const AssetInfo& asset)
    {
        LOG_INFO("[AssetBrowserUI] Asset double-clicked: %s", asset.name.c_str());
        
        // 根据资产类型执行不同操作
        switch (asset.type)
        {
            case AssetType::Model:
                // TODO: 在场景中添加模型
                LOG_INFO("[AssetBrowserUI] Would load model: %s", asset.absolutePath.c_str());
                break;
                
            case AssetType::Texture:
                // TODO: 在预览窗口显示纹理
                LOG_INFO("[AssetBrowserUI] Would preview texture: %s", asset.absolutePath.c_str());
                break;
                
            case AssetType::Level:
                // TODO: 加载关卡
                LOG_INFO("[AssetBrowserUI] Would load level: %s", asset.absolutePath.c_str());
                break;
                
            default:
                break;
        }
    }
}
