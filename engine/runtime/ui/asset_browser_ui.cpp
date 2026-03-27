#include "asset_browser_ui.h"
#include "../core/base/macro.h"
#include "../render/interface/rhi.h"
#include "../render/interface/vulkan/vulkan_rhi.h"
#include "../render/interface/vulkan/vulkan_rhi_resource.h"
#include "../global/global_context.h"
#include "../render/render_system.h"

#include <algorithm>
#include <filesystem>
#include <cstring>

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
        
        AssetManager::getInstance().scanAssets();
        
        m_initialized = true;
        m_needsRefresh = true;
        
        LOG_INFO("[AssetBrowserUI] Initialized successfully");
        return true;
    }
    
    void AssetBrowserUI::cleanup()
    {
        for (auto& pair : m_thumbnailCache)
        {
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
        
        renderSearchBar();
        
        ImGui::SameLine();
        
        renderTypeFilter();
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        
        if (m_needsRefresh)
        {
            refreshAssets();
            m_needsRefresh = false;
        }
        
        renderAssetGrid(m_currentDisplayAssets);
        
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
        ImGui::Text("Search: ");
        ImGui::SameLine();
        
        ImGui::PushItemWidth(200);
        
        static char searchBuffer[256] = "";
        if (ImGui::InputText("##AssetSearch", searchBuffer, sizeof(searchBuffer), ImGuiInputTextFlags_EnterReturnsTrue))
        {
            m_searchQuery = searchBuffer;
            m_needsRefresh = true;
        }
        
        if (m_searchQuery != searchBuffer)
        {
            std::strncpy(searchBuffer, m_searchQuery.c_str(), sizeof(searchBuffer) - 1);
            searchBuffer[sizeof(searchBuffer) - 1] = '\0';
        }
        
        if (std::strlen(searchBuffer) > 0)
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
        
        float windowVisibleX = ImGui::GetContentRegionAvail().x;
        float itemSize = m_thumbnailSize * m_zoomLevel;
        float itemSpacing = 10.0f;
        int columns = std::max(1, static_cast<int>(windowVisibleX / (itemSize + itemSpacing)));
        
        ImGui::Text("Zoom: ");
        ImGui::SameLine();
        ImGui::PushItemWidth(100);
        ImGui::SliderFloat("##Zoom", &m_zoomLevel, 0.5f, 2.0f, "%.1fx");
        ImGui::PopItemWidth();
        
        ImGui::Spacing();
        
        ImGui::BeginChild("AssetGrid", ImVec2(0, 0), true);
        
        int columnCounter = 0;
        for (const auto& asset : assets)
        {
            ImGui::PushID(asset.path.c_str());
            
            if (columnCounter > 0)
            {
                ImGui::SameLine(0.0f, itemSpacing);
            }
            
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
        
        if (isSelected)
        {
            ImVec4 selectedColor = ImVec4(0.2f, 0.4f, 0.8f, 0.5f);
            ImVec2 pos = ImGui::GetCursorScreenPos();
            ImVec2 itemFullSize = ImVec2(itemSize, itemSize + 25.0f);
            ImGui::GetWindowDrawList()->AddRectFilled(pos, ImVec2(pos.x + itemFullSize.x, pos.y + itemFullSize.y), 
                ImGui::GetColorU32(selectedColor), 5.0f);
        }
        
        ImGui::BeginGroup();
        
        ImVec2 thumbnailSize = ImVec2(itemSize, itemSize * 0.8f);
        renderThumbnail(asset, thumbnailSize);
        
        ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + itemSize);
        ImGui::Text("%s", asset.name.c_str());
        ImGui::PopTextWrapPos();
        
        if (asset.isLoaded)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "*");
        }
        
        ImGui::EndGroup();
        
        if (ImGui::IsItemClicked())
        {
            handleAssetSelection(asset);
            isSelected = true;
        }
        
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
        {
            handleAssetDoubleClick(asset);
        }
        
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
        if (asset.type == AssetType::Texture)
        {
            ImTextureID textureId = getOrCreateTextureThumbnail(asset);
            if (textureId)
            {
                ImGui::Image(textureId, size);
                return;
            }
        }
        
        renderDefaultIcon(asset.type, size);
    }
    
    ImTextureID AssetBrowserUI::getOrCreateTextureThumbnail(const AssetInfo& asset)
    {
        auto it = m_thumbnailCache.find(asset.absolutePath);
        if (it != m_thumbnailCache.end() && it->second.loaded)
        {
            return it->second.textureId;
        }
        
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
        (void)path;
        return false;
    }
    
    void AssetBrowserUI::renderDefaultIcon(AssetType type, ImVec2 size)
    {
        ImVec4 bgColor;
        const char* icon = "";
        
        switch (type)
        {
            case AssetType::Model:
                bgColor = ImVec4(0.3f, 0.5f, 0.7f, 1.0f);
                icon = "[M]";
                break;
            case AssetType::Texture:
                bgColor = ImVec4(0.5f, 0.7f, 0.3f, 1.0f);
                icon = "[T]";
                break;
            case AssetType::Material:
                bgColor = ImVec4(0.7f, 0.5f, 0.7f, 1.0f);
                icon = "[Mat]";
                break;
            case AssetType::Shader:
                bgColor = ImVec4(0.7f, 0.7f, 0.3f, 1.0f);
                icon = "[S]";
                break;
            case AssetType::Level:
                bgColor = ImVec4(0.5f, 0.5f, 0.7f, 1.0f);
                icon = "[L]";
                break;
            default:
                bgColor = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
                icon = "[?]";
                break;
        }
        
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled(
            pos, 
            ImVec2(pos.x + size.x, pos.y + size.y), 
            ImGui::GetColorU32(bgColor),
            5.0f
        );
        
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + size.y * 0.3f);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + size.x * 0.3f);
        ImGui::Text("%s", icon);
        
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
        AssetManager& assetManager = AssetManager::getInstance();
        if (!assetManager.isAssetsScanned())
        {
            assetManager.scanAssets();
        }
        
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
        
        switch (asset.type)
        {
            case AssetType::Model:
                LOG_INFO("[AssetBrowserUI] Would load model: %s", asset.absolutePath.c_str());
                break;
                
            case AssetType::Texture:
                LOG_INFO("[AssetBrowserUI] Would preview texture: %s", asset.absolutePath.c_str());
                break;
                
            case AssetType::Level:
                LOG_INFO("[AssetBrowserUI] Would load level: %s", asset.absolutePath.c_str());
                break;
                
            default:
                break;
        }
    }
}