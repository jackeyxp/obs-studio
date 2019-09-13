import Dialog from '../../vant-weapp/dialog/dialog';

// 获取全局的app对象...
const g_app = getApp()

Page({
  /**
   * 页面的初始数据
   */
  data: {
    m_code: '',
    m_arrRoom: [],
    m_cur_page: 1,
    m_max_page: 1,
    m_total_num: 0,
    m_show_more: true,
    m_show_auth: false,
    m_load_type: null,
    m_urlSite: g_app.globalData.m_urlSite
  },

  /**
   * 生命周期函数--监听页面加载
   */
  onLoad: function (options) {
    // 保存加载类型，页面加载完毕之后再跳转验证...
    this.data.m_load_type = options.type;
    // 显示加载动画，做了双层动画加载处理...
    wx.showLoading({ title: '加载中' });
    // 调用接口，获取房间列表...
    this.doAPIGetRoom()
  },

  // 获取房间列表...
  doAPIGetRoom: function () {
    // 显示导航栏|浮动加载动画...
    wx.showLoading({ title: '加载中' });
    // 保存this对象...
    var that = this
    // 准备需要的参数信息...
    var thePostData = {
      'cur_page': that.data.m_cur_page
    }
    // 构造访问接口连接地址...
    var theUrl = g_app.globalData.m_urlPrev + 'Mini/getRoom'
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
          that.setData({ m_show_more: false, m_no_more: '获取房间记录失败' })
          return
        }
        // dataType 没有设置json，需要自己转换...
        var arrData = JSON.parse(res.data);
        // 获取失败的处理 => 显示获取到的错误信息...
        if (arrData.err_code > 0) {
          that.setData({ m_show_more: false, m_no_more: arrData.err_msg })
          return
        }
        // 获取到的记录数据不为空时才进行记录合并处理 => concat 不会改动原数据
        if ((arrData.room instanceof Array) && (arrData.room.length > 0)) {
          that.data.m_arrRoom = that.data.m_arrRoom.concat(arrData.room)
        }
        // 保存获取到的记录总数和总页数...
        that.data.m_total_num = arrData.total_num
        that.data.m_max_page = arrData.max_page
        // 将数据显示到模版界面上去，并且显示加载更多页面...
        that.setData({ m_arrRoom: that.data.m_arrRoom, m_total_num: that.data.m_total_num })
        // 如果到达最大页数，关闭加载更多信息...
        if (that.data.m_cur_page >= that.data.m_max_page) {
          that.setData({ m_show_more: false, m_no_more: '没有更多内容了' })
        }
      },
      fail: function (res) {
        // 隐藏导航栏加载动画...
        wx.hideLoading()
        that.setData({ m_show_more: false, m_no_more: '获取房间记录失败' })
      }
    })
  },
  /**
   * 生命周期函数--监听页面初次渲染完成
   */
  onReady: function () {
    // 页面初次渲染完毕...
    console.log('onReady')
    //wx.hideLoading()
    //wx.hideNavigationBarLoading();
    // 注意：可能比doAPIGetRoom到达更快，故不能先关闭加载动画...
    // 如果附加参数有效，并且是来自扫码绑定，需要进一步跳转验证...
    let theAppData = g_app.globalData;
    if (this.data.m_load_type === 'bind' && theAppData.m_nUserID != null && 
      theAppData.m_curRoomItem != null && theAppData.m_userInfo != null) {
      // 如果房间对象|用户编号|用户信息，都有效，进行跳转...
      wx.navigateTo({ url: '../room/room' });
    }
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

  // 下拉刷新事件...
  onPullDownRefresh: function () {
    // 首先，打印信息，停止刷新...
    console.log('onPullDownRefresh');
    wx.stopPullDownRefresh();
    // 重新从首页开始更新数据...
    this.data.m_arrRoom = [];
    this.data.m_cur_page = 1;
    this.doAPIGetRoom();
  },

  /**
   * 页面上拉触底事件的处理函数
   */
  onReachBottom: function () {
    console.log('onReachBottom')
    // 如果到达最大页数，关闭加载更多信息...
    if (this.data.m_cur_page >= this.data.m_max_page) {
      this.setData({ m_show_more: false, m_no_more: '没有更多内容了' })
      return
    }
    // 没有达到最大页数，累加当前页码，请求更多数据...
    this.data.m_cur_page += 1
    this.doAPIGetRoom()
  },

  /**
   * 用户点击右上角分享
   */
  onShareAppMessage: function () {
  },

  // 响应登录错误接口...
  onLoginError: function (inTitle, inMessage) {
    Dialog.alert({ title: inTitle, message: inMessage });
  },

  // 响应登录正确接口...
  onLoginSuccess: function () {
    // 进行页面跳转，使用可返回的wx.navigateTo...
    wx.navigateTo({ url: '../room/room' })
  },

  // 拒绝授权|允许授权之后的回调接口...
  getUserInfo: function(res) {
    // 注意：拒绝授权，也要经过这个接口，没有res.detail.userInfo信息...
    console.log(res.detail);
    // 保存this对象...
    var that = this
    // 允许授权，通过网站接口获取用户编号...
    if (res.detail.userInfo) {
      g_app.doAPILogin(that, that.data.m_code, res.detail.userInfo, res.detail.encryptedData, res.detail.iv)
    }
  },

  // 点击房间...
  onClickRoom: function(event) {
    // 打印房间索引编号，保存房间内容到全局变量...
    console.log(event.currentTarget.id);
    g_app.globalData.m_curRoomItem = this.data.m_arrRoom[event.currentTarget.id]
    // 如果用户编号和用户信息有效，直接跳转到房间聊天页面，使用可返回的wx.navigateTo...
    if (g_app.globalData.m_nUserID > 0 && g_app.globalData.m_userInfo != null) {
      wx.navigateTo({url: '../room/room'})
      return;
    }
    // 显示加载动画过程...
    wx.showLoading({ title: '加载中' });
    // 保存this对象...
    var that = this
    // 处理登录过程...
    wx.login({
      success: res => {
        //保存code，后续使用...
        that.setData({ m_code: res.code })
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
    })
  }
})