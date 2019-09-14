
import Notify from '../../vant-weapp/notify/notify';

// 获取全局的app对象...
const g_app = getApp()

Page({

  /**
   * 页面的初始数据
   */
  data: {
    m_curUser: null,
    m_arrSex: ['无', '男宝宝', '女宝宝'],
  },

  onNameChange: function (event) {
    this.setData({ 'm_curUser.child_name': event.detail });
  },

  onNickChange: function (event) {
    this.setData({ 'm_curUser.child_nick': event.detail });
  },

  onBirthChange: function (event) {
    const { value } = event.detail;
    this.setData({ 'm_curUser.birthday': value });
  },

  onChildSexChange: function (event) {
    const { value } = event.detail;
    this.setData({ 'm_curUser.child_sex': value });
  },

  // 点击取消时的处理...
  doBtnCancel: function (event) {
    wx.navigateBack();
  },

  // 点击保存时的处理...
  doBtnSave: function (event) {
    // 保存this对象...
    let that = this
    let theCurUser = this.data.m_curUser;
    if (theCurUser.child_name == null || theCurUser.child_name.length <= 0) {
      Notify('【宝宝姓名】不能为空，请重新输入！');
      return;
    }
    if (theCurUser.child_nick == null || theCurUser.child_nick.length <= 0) {
      Notify('【宝宝小名】不能为空，请重新输入！');
      return;
    }
    if (theCurUser.birthday == null || theCurUser.birthday.length <= 0 || theCurUser.birthday == '无') {
      Notify('【宝宝生日】不能为空，请重新输入！');
      return;
    }
    if (theCurUser.child_sex <= 0) {
      Notify('【宝宝性别】不能为空，请重新选择！');
      return;
    }
    // 显示导航栏|浮动加载动画...
    wx.showLoading({ title: '加载中' });
    // 准备需要的参数信息...
    var thePostData = {
      'user_id': theCurUser.user_id,
      'birthday': theCurUser.birthday,
      'child_sex': theCurUser.child_sex,
      'child_name': theCurUser.child_name,
      'child_nick': theCurUser.child_nick,
      'child_id': (theCurUser.child_id ? theCurUser.child_id : 0),
    }
    // 构造访问接口连接地址...
    let theUrl = g_app.globalData.m_urlPrev + 'Mini/saveChild';
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
        if (res.statusCode != 200 || res.data.length <= 0) {
          Notify('更新宝宝信息失败！');
          return;
        }
        // dataType 没有设置json，需要自己转换...
        var arrData = JSON.parse(res.data);
        if (arrData.err_code > 0) {
          Notify(arrData.err_msg);
          return;
        }
        // 判断返回数据是否有效，并更新child_id...
        if (arrData.child_id == null || arrData.child_id <= 0) {
          Notify('更新宝宝信息失败！');
          return;
        }
        // 保存最新的child_id到当前用户对象当中...
        theCurUser.child_id = arrData.child_id;
        // 进行父页面的数据更新...
        let pages = getCurrentPages();
        let prevItemPage = pages[pages.length - 2];
        let prevParentPage = pages[pages.length - 3];
        let theIndex = parseInt(theCurUser.indexID);
        let theArrUser = prevParentPage.data.m_arrUser;
        let thePrevUser = theArrUser[theIndex];
        thePrevUser.child_name = theCurUser.child_name;
        thePrevUser.child_nick = theCurUser.child_nick;
        thePrevUser.child_sex = theCurUser.child_sex;
        thePrevUser.child_id = theCurUser.child_id;
        thePrevUser.birthday = theCurUser.birthday;
        prevParentPage.setData({ m_arrUser: theArrUser });
        prevItemPage.setData({ m_curUser: thePrevUser });
        // 进行页面的更新跳转...
        wx.navigateBack();
      },
      fail: function (res) {
        wx.hideLoading()
        Notify('更新宝宝信息失败！');
      }
    })
  },

  /**
   * 生命周期函数--监听页面加载
   */
  onLoad: function (options) {
    let curUser = g_app.globalData.m_curSelectItem;
    curUser.child_sex = ((curUser.child_sex == null) ? 0 : curUser.child_sex);
    wx.setNavigationBarTitle({ title: curUser.wx_nickname + ' - 宝宝信息' });
    this.setData({ m_curUser: curUser });
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