//app.js
App({
  onLaunch: function (options) {
    // 打印引导参数...
    //console.log(options)
    // 获取系统信息同步接口...
    this.globalData.m_sysInfo = wx.getSystemInfoSync()
    // 展示本地存储能力
    /*var logs = wx.getStorageSync('logs') || []
    logs.unshift(Date.now())
    wx.setStorageSync('logs', logs)*/
  },
  // 全局存储...
  globalData: {
    m_urlSite: 'https://edu.ihaoyi.cn/',
    m_urlPrev: 'https://edu.ihaoyi.cn/wxapi.php/',
    m_nMasterAgentID: 0,        // 管理的机构编号...
    m_nMasterShopID: 0,         // 管理的门店编号...
    m_curSelectItem: null,
    m_curRoomItem: null,
    m_scanSockFD: null,
    m_scanTimeID: null,
    m_scanType: null,
    m_userInfo: null,
    m_sysInfo: null,
    m_nUserID: 0,
    m_userTypeID: {
      kParentUser: 0, kAssistUser: 1, kTeacherUser: 2,
      kShopMasterUser: 3, kShopOwnerUser: 4, kMaintainUser: 5, kAdministerUser: 6
    },
    m_userTypeName: ['家长', '助教', '讲师', '校长', '学校老板', '运营维护者', '系统管理员'],
    m_parentTypeName: ['无', '妈妈', '爸爸', '亲属']
  },
  // 登录接口...
  doAPILogin: function (inPage, inCode, inUserInfo, inEncrypt, inIV) {
    // 显示导航栏|浮动加载动画...
    wx.showLoading({ title: '加载中' });
    // 保存this对象...
    var g_app = this;
    var that = inPage;
    // 获取系统信息同步接口...
    var theSysInfo = g_app.globalData.m_sysInfo
    // 准备需要的参数信息 => 加入一些附加信息...
    var thePostData = {
      iv: inIV,
      code: inCode,
      encrypt: inEncrypt,
      wx_brand: theSysInfo.brand,
      wx_model: theSysInfo.model,
      wx_version: theSysInfo.version,
      wx_system: theSysInfo.system,
      wx_platform: theSysInfo.platform,
      wx_SDKVersion: theSysInfo.SDKVersion,
      wx_pixelRatio: theSysInfo.pixelRatio,
      wx_screenWidth: theSysInfo.screenWidth,
      wx_screenHeight: theSysInfo.screenHeight,
      wx_fontSizeSetting: theSysInfo.fontSizeSetting
    }
    // 构造访问接口连接地址...
    var theUrl = g_app.globalData.m_urlPrev + 'Mini/login'
    // 请求远程API过程...
    wx.request({
      url: theUrl,
      method: 'POST',
      data: thePostData,
      dataType: 'x-www-form-urlencoded',
      header: { 'content-type': 'application/x-www-form-urlencoded' },
      success: function (res) {
        console.log(res);
        // 隐藏导航栏加载动画...
        wx.hideLoading();
        // 如果返回数据无效或状态不对，打印错误信息，直接返回...
        if (res.statusCode != 200 || res.data.length <= 0) {
          that.onLoginError('错误警告', '调用网站登录接口失败！');
          return
        }
        // dataType 没有设置json，需要自己转换...
        var arrData = JSON.parse(res.data);
        // 获取授权数据失败的处理...
        if (arrData.err_code > 0) {
          that.onLoginError('错误警告', arrData.err_msg);
          return
        }
        // 获取授权数据成功，保存用户编号|用户类型|真实姓名...
        g_app.globalData.m_nMasterAgentID = arrData.master_agent_id
        g_app.globalData.m_nMasterShopID = arrData.master_shop_id
        g_app.globalData.m_nUserID = arrData.user_id
        g_app.globalData.m_userInfo = inUserInfo
        g_app.globalData.m_userInfo.userType = arrData.user_type
        g_app.globalData.m_userInfo.realName = arrData.real_name
        // 处理成功的操作...
        that.onLoginSuccess();
      },
      fail: function (res) {
        console.log(res);
        // 隐藏导航栏加载动画...
        wx.hideLoading();
        // 打印错误信息，显示错误警告...
        that.onLoginError('错误警告', '调用网站登录接口失败！');
      }
    })
  }
})