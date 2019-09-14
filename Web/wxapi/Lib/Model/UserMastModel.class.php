<?php

class UserMastModel extends ViewModel {
    public $viewFields = array(
        'User'=>array('*','_type'=>'LEFT'), //Must be LEFT 返回左表所有数据，即使右表没有匹配的
        'Shop'=>array('shop_id'=>'master_shop_id','_on'=>'User.user_id=Shop.master_id','_type'=>'LEFT'),
        'Agent'=>array('agent_id'=>'master_agent_id','_on'=>'User.user_id=Agent.master_id')
    );
}
?>