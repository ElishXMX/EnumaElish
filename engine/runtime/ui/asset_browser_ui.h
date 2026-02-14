#pragma once

#include "../core/asset/asset_manager.h"
#include <imgui.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

namespace Elish
{
    class RHI;
    class RHIImage;
    class RHIImageView;
    class RHISampler;
    
    /**
     * @brief 资产浏览器UI组件
     * 
     * 提供资产的可视化浏览功能，包括：
     * - 资产网格视图（带缩略图）
     * - 资产类型过滤
     * - 搜索功能
     * - 资产详情显示
     * 
     * 输出：渲染资产浏览器UI界面
     */
    class AssetBrowserUI
    {
    public:
        /**
         * @brief 获取单例实例
         * @return 资产浏览器UI实例引用
         */
        static AssetBrowserUI& getInstance()
        {
            static AssetBrowserUI instance;
            return instance;
        }
        
        /**
         * @brief 初始化资产浏览器
         * @param rhi RHI接口指针
         * @return 初始化是否成功
         */
        bool initialize(std::shared_ptr<RHI> rhi);
        
        /**
         * @brief 清理资源
         */
        void cleanup();
        
        /**
         * @brief 渲染资产浏览器UI
         * 在底部面板中显示资产浏览器
         */
        void render();
        
        /**
         * @brief 设置资产浏览器可见性
         * @param visible 是否可见
         */
        void setVisible(bool visible) { m_visible = visible; }
        
        /**
         * @brief 获取资产浏览器可见性
         * @return 是否可见
         */
        bool isVisible() const { return m_visible; }
        
        /**
         * @brief 获取当前选中的资产路径
         * @return 选中的资产路径，未选中返回空字符串
         */
        const std::string& getSelectedAssetPath() const { return m_selectedAssetPath; }
        
        /**
         * @brief 刷新资产列表
         */
        void refreshAssets();
        
    private:
        AssetBrowserUI() = default;
        ~AssetBrowserUI() = default;
        AssetBrowserUI(const AssetBrowserUI&) = delete;
        AssetBrowserUI& operator=(const AssetBrowserUI&) = delete;
        
        /**
         * @brief 渲染资产网格视图
         * @param assets 要显示的资产列表
         */
        void renderAssetGrid(const std::vector<AssetInfo>& assets);
        
        /**
         * @brief 渲染单个资产项
         * @param asset 资产信息
         * @param itemSize 项大小
         * @return 是否被选中
         */
        bool renderAssetItem(const AssetInfo& asset, float itemSize);
        
        /**
         * @brief 渲染资产详情面板
         */
        void renderAssetDetails();
        
        /**
         * @brief 渲染缩略图
         * @param asset 资产信息
         * @param size 显示大小
         */
        void renderThumbnail(const AssetInfo& asset, ImVec2 size);
        
        /**
         * @brief 获取或创建纹理缩略图
         * @param asset 资产信息
         * @return ImGui纹理ID
         */
        ImTextureID getOrCreateTextureThumbnail(const AssetInfo& asset);
        
        /**
         * @brief 加载纹理文件为缩略图
         * @param path 纹理文件路径
         * @return 是否成功加载
         */
        bool loadTextureThumbnail(const std::string& path);
        
        /**
         * @brief 渲染默认图标
         * @param type 资产类型
         * @param size 显示大小
         */
        void renderDefaultIcon(AssetType type, ImVec2 size);
        
        /**
         * @brief 渲染搜索栏
         */
        void renderSearchBar();
        
        /**
         * @brief 渲染类型过滤器
         */
        void renderTypeFilter();
        
        /**
         * @brief 处理资产选择
         * @param asset 选中的资产
         */
        void handleAssetSelection(const AssetInfo& asset);
        
        /**
         * @brief 处理资产双击
         * @param asset 双击的资产
         */
        void handleAssetDoubleClick(const AssetInfo& asset);

    private:
        bool m_initialized = false;             // 是否已初始化
        bool m_visible = true;                  // 是否可见
        std::shared_ptr<RHI> m_rhi;             // RHI接口
        
        // UI状态
        int m_selectedTypeFilter = 0;           // 类型过滤器索引
        std::string m_searchQuery;              // 搜索关键词
        std::string m_selectedAssetPath;        // 选中的资产路径
        const AssetInfo* m_selectedAsset = nullptr;  // 选中的资产信息
        float m_thumbnailSize = 80.0f;          // 缩略图大小
        float m_zoomLevel = 1.0f;               // 缩放级别
        
        // 缩略图缓存
        struct ThumbnailCache
        {
            RHIImage* image = nullptr;
            RHIImageView* imageView = nullptr;
            RHISampler* sampler = nullptr;
            ImTextureID textureId = nullptr;
            bool loaded = false;
        };
        std::unordered_map<std::string, ThumbnailCache> m_thumbnailCache;
        
        // 当前显示的资产列表（缓存）
        std::vector<AssetInfo> m_currentDisplayAssets;
        bool m_needsRefresh = true;             // 是否需要刷新显示列表
    };
}
