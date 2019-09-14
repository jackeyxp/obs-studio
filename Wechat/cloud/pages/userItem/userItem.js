
import Notify from '../../vant-weapp/notify/notify';

// 获取全局的app对象...
const g_appData = getApp().globalData;

Page({
  /**
   * 页面的初始数据
   */
  data: {
    m_bIsAdmin: false,
    m_curUser: null,
    m_arrShop: [],
    m_curShopID: 0,
    m_curShopName: '',
    m_userTypeName: g_appData.m_userTypeName,
  },

  /**
   * 生命周期函数--监听页面加载
   */
  onLoad: function (options) {
    // 显示导航栏|浮动加载动画...
    wx.showLoading({ title: '加载中' });
    // 保存this对象...
    let that = this
    // 构造访问接口连接地址...
    let theUrl = g_appData.m_urlPrev + 'Mini/getAllShop';
    // 请求远程API过程...
    wx.request({
      url: theUrl,
      method: 'GET',
      dataType: 'x-www-form-urlencoded',
      header: { 'content-type': 'application/x-www-form-urlencoded' },
      success: function (res) {
        // 隐藏导航栏加载动画...
        wx.hideLoading();
        // 调用接口失败...
        if (res.statusCode != 200) {
          Notify('获取学校列表信息失败！');
          return;
        }
        // dataType 没有设置json，需要自己转换...
        let arrShop = JSON.parse(res.data);
        if (!(arrShop instanceof Array)) {
          arrShop = [];
        }
        // 保证输入必须是数组类型...
        that.doShowUser(arrShop);
      },
      fail: function (res) {
        // 隐藏导航栏加载动画...
        wx.hideLoading();
        Notify('获取学校列表信息失败！');
      }
    });
  },

  doShowUser: function (arrShop) {
    let loginUser = g_appData.m_userInfo;
    let curUser = g_appData.m_curSelectItem;
    let strTitle = curUser.wx_nickname + ' - 用户信息';
    let bIsAdmin = ((loginUser.userType >= g_appData.m_userTypeID.kMaintainUser) ? true : false);
    let theCurShopName = curUser.shop_name;
    let theCurShopID = curUser.shop_id;
    let theCurShopIndex = 0;
    // 修改标题信息...
    wx.setNavigationBarTitle({ title: strTitle });
    // 需要找到当前用户所在的学校索引编号...
    if (arrShop.length > 0) {
      for (let index = 0; index < arrShop.length; ++index) {
        if (arrShop[index].shop_id == theCurShopID) {
          theCurShopIndex = index;
          break;
        }
      }
    }
    // 应用到界面...
    this.setData({
      m_curUser: curUser,
      m_bIsAdmin: bIsAdmin,
      m_arrShop: arrShop,
      m_curShopID: theCurShopID,
      m_curShopName: theCurShopName,
      m_curShopIndex: theCurShopIndex,
    });
  },

  // 用户类型发生变化时的响应接口...
  onUserTypeChange: function (event) {
    const { value } = event.detail;
    this.setData({ 'm_curUser.user_type': value });
    // 进行数据库修改更新...
    this.doSaveUser();
  },

  // 所属学校发生选择变化...
  onShopChange: function (event) {
    const { value } = event.detail;
    let theNewShopID = parseInt(value);
    if (theNewShopID < 0 || theNewShopID >= this.data.m_arrShop.length) {
      Notify('【所属学校】选择内容越界！');
      return;
    }
    // 获取到当前变化后的学校信息，并写入数据然后显示出来...
    let theCurShop = this.data.m_arrShop[theNewShopID];
    this.setData({ m_curShopID: theCurShop.shop_id, m_curShopName: theCurShop.name });
    // 进行数据库修改更新...
    this.doSaveUser();
  },

  // 直接保存用户信息接口...
  doSaveUser: function () {
    // 保存this对象...
    let that = this;
    let theCurUser = this.data.m_curUser;
    // 显示导航栏|浮动加载动画...
    wx.showLoading({ title: '加载中' });
    // 准备需要的参数信息...
    var thePostData = {
      'user_id': theCurUser.user_id,
      'user_type': theCurUser.user_type,
      'shop_id': that.data.m_curShopID,
    }
    // 构造访问接口连接地址...
    let theUrl = g_appData.m_urlPrev + 'Mini/saveUser';
    // 请求远程API过程...
    wx.request({
      url: theUrl,
      method: 'POST',
      data: thePostData,
      dataType: 'x-www-form-urlencoded',
      header: { 'content-type': 'application/x-www-form-urlencoded' },
      success: function (res) {
        // 隐藏导航栏加载动画...
        wx.hideLoading();
        // 调用接口失败...
        if (res.statusCode != 200) {
          Notify('更新用户记录失败！');
          return;
        }
        // 进行父页面的数据更新...
        let pages = getCurrentPages();
        let prevParentPage = pages[pages.length - 2];
        let theIndex = parseInt(theCurUser.indexID);
        let theArrUser = prevParentPage.data.m_arrUser;
        let thePrevUser = theArrUser[theIndex];
        thePrevUser.user_type = theCurUser.user_type;
        thePrevUser.shop_id = that.data.m_curShopID;
        thePrevUser.shop_name = that.data.m_curShopName;
        prevParentPage.setData({ m_arrUser: theArrUser });
        thePrevUser.indexID = theCurUser.indexID;
        that.setData({ m_curUser: thePrevUser });
        g_appData.m_curSelectItem = thePrevUser;
        g_appData.m_userInfo.userType = theCurUser.user_type;
      },
      fail: function (res) {
        wx.hideLoading()
        Notify('更新用户记录失败！');
      }
    })
  },
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