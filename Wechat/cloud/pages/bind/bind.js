
import { $wuxKeyBoard } from '../../wux-weapp/index'
import Notify from '../../vant-weapp/notify/notify';

// 定义绑定登录子命令...
const BIND_SCAN = 1
const BIND_SAVE = 2
const BIND_CANCEL = 3

// 定义终端类型...
const kClientStudent = 2
const kClientTeacher = 3

// 获取全局的app对象...
const g_app = getApp()

Page({
  /**
   * 页面的初始数据
   */
  data: {
    icon: {
      type: 'warn',
      color: '#ef473a',
    },
    buttons: [{
      type: 'balanced',
      block: true,
      text: '点击授权',
      openType: 'getUserInfo',
    }],
    btnScan: [{
      type: 'balanced',
      outline: false,
      block: true,
      text: '重新扫描',
    },{
      type: 'stable',
      outline: true,
      block: true,
      text: '取消',
    }],
    m_code: '',
    m_btnClick: '',
    m_show_auth: false,
  },

  // 用户点击重新扫描或取消...
  doClickScan: function (inEvent) {
    // const { index } = inEvent.detail;
    // 用户点击取消按钮 => 返回首页...
    if (inEvent.detail.index === 1) {
      wx.reLaunch({ url: '../home/home' });
      return;
    }
    // 点击重新扫描...
    wx.scanCode({
      onlyFromCamera: true,
      success: (res) => {
        // 打印扫描结果...
        console.log(res)
        // 如果路径有效，直接跳转到相关页面 => path 已确定会带参数...
        if (typeof res.path != 'undefined' && res.path.length > 0) {
          res.path = '../../' + res.path
          wx.redirectTo({ url: res.path })
        }
      },
      fail: (res) => {
        console.log(res)
      }
    })
  },
  /**
   * 生命周期函数--监听页面加载
   */
  onLoad: function (options) {
    console.log(options);
    // 解析二维码场景值 => type_socket_rand
    let arrData = options.scene.split('_');
    // 判断场景值 => 必须是数组，必须是3个字段...
    if (!(arrData instanceof Array) || (arrData.length != 3) ||
      (arrData[0] != kClientStudent && arrData[0] != kClientTeacher)) {
      // 场景值格式不正确，需要重新扫描...
      this.setData({
        m_show_auth: true,
        m_title: "扫码登录失败",
        m_label: "您扫描的二维码格式不正确，请确认后重新扫描！",
        m_btnClick: 'doClickScan',
        buttons: this.data.btnScan,
      });
      return;
    }
    // 将解析的场景值进行分别存储...
    let theAppData = g_app.globalData;
    theAppData.m_scanType = parseInt(arrData[0]);
    theAppData.m_scanSockFD = parseInt(arrData[1]);
    theAppData.m_scanTimeID = parseInt(arrData[2]);
    // 注意：这里进行了屏蔽，只要扫码完成，就需要完成全部验证过程，不要简化...
    // 如果用户编号和用户信息有效，直接跳转到房间聊天页面，使用不可返回的wx.reLaunch...
    //if (theAppData.m_nUserID > 0 && theAppData.m_userInfo != null && theAppData.m_curRoomItem != null) {
    //  wx.reLaunch({ url: '../home/home?type=bind' })
    //  return;
    //}
    // 用户编号|用户信息|房间信息，只要无效，开始弹框授权...
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
            that.setData({
              m_show_auth: true,
              m_title: "授权失败",
              m_label: "本小程序需要您的授权，请点击授权按钮完成授权。",
              buttons: that.data.buttons
            });
          }
        })
      }
    })
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
  onLoginError: function (inTitle, inMessage) {
    this.doBindError(inTitle, inMessage);
  },

  // 响应登录正确接口...
  onLoginSuccess: function () {
    let theAppData = g_app.globalData;
    // 如果是讲师端扫描，用户类型必须是讲师，绑定房间必须有效...
    if (theAppData.m_scanType === kClientTeacher) {
      // 如果用户身份低于讲师类型，需要弹框警告...
      if (parseInt(theAppData.m_userInfo.userType) < theAppData.m_userTypeID.kTeacherUser) {
        this.doBindError("错误警告", "讲师端软件，只有讲师身份的用户才能使用，请联系经销商，获取讲师身份授权！");
        return;
      }
    }
    // 向对应终端发送扫码成功子命令...
    this.doAPIBindMini(BIND_SCAN);
  },

  // 转发绑定子命令到对应的终端对象...
  doAPIBindMini: function (inSubCmd, inRoomID = 0) {
    // 获取到的用户信息有效，弹出等待框...
    wx.showLoading({ title: '加载中' })
    wx.showNavigationBarLoading()
    // 保存this对象...
    var strError = ''
    var that = this
    // 准备需要的参数信息...
    var thePostData = {
      'client_type': g_app.globalData.m_scanType,
      'tcp_socket': g_app.globalData.m_scanSockFD,
      'tcp_time': g_app.globalData.m_scanTimeID,
      'user_id': g_app.globalData.m_nUserID,
      'bind_cmd': inSubCmd,
      'room_id': inRoomID
    }
    // 构造访问接口连接地址 => 通过PHP转发给中心 => 中心再转发给终端...
    var theUrl = g_app.globalData.m_urlPrev + 'Mini/bindLogin'
    // 请求远程API过程...
    wx.request({
      url: theUrl,
      method: 'POST',
      data: thePostData,
      dataType: 'x-www-form-urlencoded',
      header: { 'content-type': 'application/x-www-form-urlencoded' },
      success: function (res) {
        wx.hideLoading();
        wx.hideNavigationBarLoading();
        // 调用接口失败 => 直接返回
        let strTitle = "中转命令发送失败";
        if (res.statusCode != 200) {
          strError = '发送命令失败，错误码：' + res.statusCode;
          that.doBindError(strTitle, strError, (inSubCmd == BIND_SAVE));
          return
        }
        // dataType 没有设置json，需要自己转换...
        var arrData = JSON.parse(res.data);
        // 汇报反馈失败的处理 => 直接返回...
        if (arrData.err_code > 0) {
          strError = arrData.err_msg + ' 错误码：' + arrData.err_code;
          that.doBindError(strTitle, strError, (inSubCmd == BIND_SAVE));
          return
        }
        // 注意：这里必须使用reLaunch，redirectTo不起作用...
        if (inSubCmd == BIND_SAVE) {
          wx.reLaunch({ url: '../home/home?type=bind' })
        } else if (inSubCmd == BIND_CANCEL) {
          // 如果点了 取消 直接跳转到无参数首页页面...
          that.doBindError("您已取消此次登录", "您可再次扫描登录，或关闭窗口！")
        } else if (inSubCmd == BIND_SCAN) {
          // 扫码授权成功之后，更改标题，输入密码...
          wx.setNavigationBarTitle({ title: '扫码登录 - 房间密码' });
          that.setData({ m_show_auth: 2 });
          // 显示明文，输完数字会自动回调...
          $wuxKeyBoard().show({
            showCancel: true,
            password: false,
            callback(value) {
              that.doAPIRoomPass(value);
            },
            onCancel(value) {
              that.doAPIBindMini(BIND_CANCEL);
            }
          });
        }
      },
      fail: function (res) {
        wx.hideLoading();
        wx.hideNavigationBarLoading();
        // 打印错误信息，显示错误警告...
        that.doBindError("错误警告", "调用网站通知接口失败！")
      }
    })
  },
  // 发送到网站端验证房间密码是否正确...
  doAPIRoomPass: function (inPass) {
    // 获取到的用户信息有效，弹出等待框...
    wx.showLoading({ title: '密码验证中' })
    // 保存this对象...
    var that = this
    // 准备需要的参数信息...
    var thePostData = {
      'room_pass': inPass
    }
    // 构造访问接口连接地址...
    var theUrl = g_app.globalData.m_urlPrev + 'Mini/roomPass'
    // 请求远程API过程...
    wx.request({
      url: theUrl,
      method: 'POST',
      data: thePostData,
      dataType: 'x-www-form-urlencoded',
      header: { 'content-type': 'application/x-www-form-urlencoded' },
      success: function (res) {
        wx.hideLoading();
        // 调用接口失败 => 直接返回
        if (res.statusCode != 200) {
          Notify(`发送命令失败，错误码：${res.statusCode}`)
          return
        }
        // dataType 没有设置json，需要自己转换...
        var arrData = JSON.parse(res.data);
        // 汇报反馈失败的处理 => 直接返回...
        if (arrData.err_code > 0) {
          Notify(arrData.err_msg);
          return
        }
        // 保存房间，然后通知中心服务器登录完成...
        g_app.globalData.m_curRoomItem = arrData.room;
        that.doAPIBindMini(BIND_SAVE, arrData.room.room_id);
      },
      fail: function (res) {
        wx.hideLoading();
        // 打印错误信息，显示错误警告...
        Notify("调用网站验证接口失败！")
      }
    })
  },
  // 显示绑定错误时的页面信息...
  doBindError: function (inTitle, inError, inShowDlg = false) {
    if (inShowDlg) {
      Notify(inError);
    } else {
      this.setData({
        m_show_auth: true,
        m_title: inTitle,
        m_label: inError,
        m_btnClick: 'doClickScan',
        buttons: this.data.btnScan,
        'icon.color': '#ef473a',
        'icon.type': 'warn',
      });
    }
  },

  // 获取房间列表...
  /*doAPIGetRoom: function () {
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
        that.setData({ m_arrRoom: that.data.m_arrRoom })
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

  // 点击房间...
  onClickRoom: function (event) {
    // 保存临时对象...
    let that = this;
    let objCurRoom = that.data.m_arrRoom[event.currentTarget.id];
    // 弹框确认是否要选择当前房间进行登录...
    Dialog.confirm({
      confirmButtonText: "登录",
      title: "房间教室：" + objCurRoom.room_id,
      message: "确实要登录当前选中的房间教室？"
    }).then(() => {
      // 保存房间，然后通知中心服务器登录完成...
      g_app.globalData.m_curRoomItem = objCurRoom;
      this.doAPIBindMini(BIND_SAVE, objCurRoom.room_id);
    }).catch(() => {
      console.log("Dialog - Cancel");
    });
  },*/

  /**
   * 页面上拉触底事件的处理函数
   */
  onReachBottom: function () {
    console.log('onReachBottom')
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
   * 用户点击右上角分享
   */
  onShareAppMessage: function () {
  }
})