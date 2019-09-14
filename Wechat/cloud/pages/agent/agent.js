
import Notify from '../../vant-weapp/notify/notify';
import Dialog from '../../vant-weapp/dialog/dialog';

// 获取全局的app对象...
const g_appData = getApp().globalData

Page({
  /**
   * 页面的初始数据
   */
  data: {
    m_arrAgent: [],
    m_cur_page: 1,
    m_max_page: 1,
    m_total_num: 0,
    m_show_more: true,
    m_no_more: '正在加载...',
    m_urlSite: g_appData.m_urlSite
  },

  // 响应点击新增机构...
  doAddAgent: function () {
    g_appData.m_curSelectItem = null;
    wx.navigateTo({ url: '../agentItem/agentItem?edit=0' });
  },

  // 响应点击修改机构...
  doModAgent: function (event) {
    let theID = event.currentTarget.id;
    let theItem = this.data.m_arrAgent[theID];
    theItem.indexID = theID;
    g_appData.m_curSelectItem = theItem;
    wx.navigateTo({ url: '../agentItem/agentItem?edit=1' });
  },

  // 响应点击续费充值...
  doPayAgent: function (event) {
    let theID = event.currentTarget.id;
    let theItem = this.data.m_arrAgent[theID];
    theItem.indexID = theID;
    g_appData.m_curSelectItem = theItem;
    wx.navigateTo({ url: '../agentPay/agentPay' });
  },

  // 响应点击消费记录...
  doCostAgent: function (event) {
    let theID = event.currentTarget.id;
    let theItem = this.data.m_arrAgent[theID];
    theItem.indexID = theID;
    g_appData.m_curSelectItem = theItem;
    wx.navigateTo({ url: '../agentCost/agentCost' });
  },

  // 点击滑动删除事件...
  onSwipeClick: function (event) {
    // 如果不是右侧，直接返回...
    const { id: nIndexID } = event.currentTarget;
    const { title: strTitle } = event.currentTarget.dataset;
    if ("right" != event.detail || nIndexID < 0)
      return;
    // 弹框确认输出...
    Dialog.confirm({
      confirmButtonText: "删除",
      title: "机构名称：" + strTitle,
      message: "确实要删除当前选中的机构记录吗？"
    }).then(() => {
      this.doDelAgent(nIndexID);
    }).catch(() => {
      console.log("Dialog - Cancel");
    });
  },
  // 进行机构的删除操作...
  doDelAgent: function (inIndexID) {
    // 显示导航栏|浮动加载动画...
    wx.showLoading({ title: '加载中' });
    // 保存this对象...
    let that = this
    let theCurAgent = this.data.m_arrAgent[inIndexID];
    // 准备需要的参数信息...
    var thePostData = {
      'agent_id': theCurAgent.agent_id,
      'qrcode': theCurAgent.qrcode,
    }
    // 构造访问接口连接地址...
    let theUrl = g_appData.m_urlPrev + 'Mini/delAgent';
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
          Notify('删除机构记录失败！');
          return;
        }
        // 删除并更新机构记录到界面当中...
        that.data.m_arrAgent.splice(inIndexID, 1);
        let theArrAgent = that.data.m_arrAgent;
        that.setData({
          m_arrAgent: theArrAgent,
          m_total_num: theArrAgent.length
        });
      },
      fail: function (res) {
        wx.hideLoading()
        Notify('删除机构记录失败！');
      }
    })
  },
  /**
   * 生命周期函数--监听页面加载
   */
  onLoad: function (options) {
    this.doAPIGetAgent();
  },

  // 获取机构列表...
  doAPIGetAgent: function () {
    // 显示导航栏|浮动加载动画...
    wx.showLoading({ title: '加载中' });
    // 保存this对象...
    var that = this
    // 准备需要的参数信息...
    var thePostData = {
      'cur_page': that.data.m_cur_page
    }
    // 构造访问接口连接地址...
    var theUrl = g_appData.m_urlPrev + 'Mini/getAgent'
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
          that.setData({ m_show_more: false, m_no_more: '获取机构记录失败' })
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
        if ((arrData.agent instanceof Array) && (arrData.agent.length > 0)) {
          that.data.m_arrAgent = that.data.m_arrAgent.concat(arrData.agent)
        }
        // 保存获取到的记录总数和总页数...
        that.data.m_total_num = arrData.total_num
        that.data.m_max_page = arrData.max_page
        // 将数据显示到模版界面上去，并且显示加载更多页面...
        that.setData({ m_arrAgent: that.data.m_arrAgent, m_total_num: that.data.m_total_num })
        // 如果到达最大页数，关闭加载更多信息...
        if (that.data.m_cur_page >= that.data.m_max_page) {
          that.setData({ m_show_more: false, m_no_more: '' })
        }
      },
      fail: function (res) {
        // 隐藏导航栏加载动画...
        wx.hideLoading()
        that.setData({ m_show_more: false, m_no_more: '获取机构记录失败' })
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
    console.log('onReachBottom')
    // 如果到达最大页数，关闭加载更多信息...
    if (this.data.m_cur_page >= this.data.m_max_page) {
      this.setData({ m_show_more: false, m_no_more: '没有更多内容了' })
      return
    }
    // 没有达到最大页数，累加当前页码，请求更多数据...
    this.data.m_cur_page += 1
    this.doAPIGetAgent()
  },

  /**
   * 用户点击右上角分享
   */
  onShareAppMessage: function () {

  }
})