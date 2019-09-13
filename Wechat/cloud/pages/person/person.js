
import Dialog from '../../vant-weapp/dialog/dialog';

// 获取全局的app对象...
const g_app = getApp()

Page({

  /**
   * 页面的初始数据
   */
  data: {
    m_code: '',
    m_bgColor: '#fff',
    m_show_auth: false,
    m_icon: {
      type: 'warn',
      color: '#ef473a',
    },
    m_buttons: [{
      type: 'balanced',
      block: true,
      text: '点击授权',
      openType: 'getUserInfo',
    }],

    /*label: '支付管理',
      data: 'navOrder',
      icon: 'fa-money',
      label: '幼儿园管理',
      data: 'navShop',
      icon: 'fa-home',
      label: '我的帐号',
      data: 'navAccount',
      icon: 'fa-user-o',*/
    /*m_UserGrids: [{
      label: '我的帐号',
      data: 'navAccount',
      icon: 'fa-user-o',
    }, {
      label: '我的订单',
      data: 'navOrder',
      icon: 'fa-money',
    }, {
      label: '课时查询',
      data: 'navQuery',
      icon: 'fa-search',
    }, {
      label: '宝宝风采',
      data: 'navBaby',
      icon: 'fa-child',
    }],*/
    /*m_ShopGrids: [{
      label: '用户管理',
      data: 'navMember',
      icon: 'fa-id-card-o',
    }, {
      label: '直播间管理',
      data: 'navLive',
      icon: 'fa-video-camera',
    }],
    m_AdminGrids: [{
      label: '用户管理',
      data: 'navMember',
      icon: 'fa-id-card-o',
    }, {
      label: '直播间管理',
      data: 'navLive',
      icon: 'fa-video-camera',
    }, {
      label: '班级管理',
      data: 'icon-blue',
      icon: 'friends-o',
      link: '../grade/grade',
    }, {
      label: '幼儿园管理',
      data: 'navShop',
      icon: 'fa-home',
    }, {
      label: '机构管理',
      data: 'navAgent',
      icon: 'fa-graduation-cap',
    }],*/
    
    m_myAgent: {
      label: '我的机构',
      data: 'icon-green',
      icon: 'home-o',
      link: '../agentBill/agentBill',
    },

    m_UserGrids: [],

    m_ShopGrids: [{
      label: '用户管理',
      data: 'icon-blue',
      icon: 'user-o',
      link: '../member/member',
    }, {
      label: '直播间管理',
      data: 'icon-red',
      icon: 'tv-o',
      link: '../live/live',
    }],
    
    m_AdminGrids: [{
      label: '用户管理',
      data: 'icon-green',
      icon: 'user-o',
      link: '../member/member',
    }, {
      label: '直播间管理',
      data: 'icon-red',
      icon: 'tv-o',
      link: '../live/live',
    }, {
      label: '幼儿园管理',
      data: 'icon-green',
      icon: 'shop-o',
      link: '../shop/shop',
    }, {
      label: '机构管理',
      data: 'icon-blue',
      icon: 'wap-home',
      link: '../agent/agent',
    }]
  },

  /**
   * 生命周期函数--监听页面加载
   */
  onLoad: function (options) {
    let theAppData = g_app.globalData;
    // 如果用户编号和用户信息都是有效的，直接显示正常页面...
    if (theAppData.m_nUserID > 0 && theAppData.m_userInfo != null) {
      this.onLoginSuccess();
      return;
    }
    // 开始登录过程，加载动画...
    wx.showLoading({ title: '加载中' });
    let that = this;
    wx.login({
      success: res => {
        that.setData({ m_code: res.code });
        // 立即读取用户信息，第一次会弹授权框...
        wx.getUserInfo({
          lang: 'zh_CN',
          withCredentials: true,
          success: res => {
            console.log(res);
            wx.hideLoading();
            // 获取成功，通过网站接口获取用户编号...
            g_app.doAPILogin(that, that.data.m_code, res.userInfo, res.encryptedData, res.iv);
          },
          fail: res => {
            console.log(res);
            wx.hideLoading();
            // 获取失败，显示授权对话框...
            that.setData({ m_show_auth: true });
          }
        })
       }
    });
  },

  // 拒绝授权|允许授权之后的回调接口...
  getUserInfo: function (res) {
    // 注意：拒绝授权，也要经过这个接口，没有res.detail.userInfo信息...
    console.log(res.detail);
    // 保存this对象...
    var that = this
    // 允许授权，通过网站接口获取用户编号...
    if (res.detail.userInfo) {
      g_app.doAPILogin(that, that.data.m_code, res.detail.userInfo, res.detail.encryptedData, res.detail.iv)
    }
  },

  // 响应登录错误接口...
  onLoginError: function(inTitle, inMessage) {
    Dialog.alert({ title: inTitle, message: inMessage });
  },

  // 响应登录正确接口...
  onLoginSuccess: function() {
    let theAppData = g_app.globalData;
    let theGrids = this.data.m_UserGrids;
    let theTypeID = theAppData.m_userTypeID;
    let theUserType = parseInt(theAppData.m_userInfo.userType);
    switch(theUserType) {
      case theTypeID.kParentUser:
      case theTypeID.kAssistUser:
      case theTypeID.kTeacherUser: theGrids = this.data.m_UserGrids; break;
      case theTypeID.kShopMasterUser:
      case theTypeID.kShopOwnerUser: theGrids = this.data.m_ShopGrids; break;
      case theTypeID.kMaintainUser:
      case theTypeID.kAdministerUser: theGrids = this.data.m_AdminGrids; break;
    }
    // 如果用户可以管理机构，需要追加条目...
    if (theAppData.m_nMasterAgentID > 0) {
      theGrids.push(this.data.m_myAgent);
    }
    // 更新到界面...
    this.setData({ 
      m_show_auth: 2, 
      m_bgColor: '#eee',
      m_grids: theGrids,
      m_userInfo: theAppData.m_userInfo,
    });
  },

  // 点击导航栏事件...
  /*toggleGridNav: function(event) {
    let navItem = event.currentTarget.dataset['item'];
    if (navItem === 'navAgent') {
      wx.navigateTo({ url: '../agent/agent' });
    } else if (navItem === 'navShop') {
      wx.navigateTo({ url: '../shop/shop' });
    } else if (navItem === 'navLive') {
      wx.navigateTo({ url: '../live/live' });
    } else if (navItem === 'navMember') {
      wx.navigateTo({ url: '../member/member' });
    }
    console.log(navItem);
  },*/

  /**
   * 生命周期函数--监听页面初次渲染完成
   */
  onReady: function () {

  },

  /**
   * 生命周期函数--监听页面显示
   */
  onShow: function () {

  },

  /**
   * 生命周期函数--监听页面隐藏
   */
  onHide: function () {

  },

  /**
   * 生命周期函数--监听页面卸载
   */
  onUnload: function () {

  },

  /**
   * 页面相关事件处理函数--监听用户下拉动作
   */
  onPullDownRefresh: function () {
  },

  /**
   * 页面上拉触底事件的处理函数
   */
  onReachBottom: function () {

  },

  /**
   * 用户点击右上角分享
   */
  onShareAppMessage: function () {

  }
})