/* Copyright (c) 2017, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/firmware.h>
#include <cam_sensor_cmn_header.h>
#include "cam_ois_mcu_thread.h"
#include "cam_ois_mcu_core.h"
#include "cam_ois_soc.h"
#include "cam_ois_core.h"
#include "cam_sensor_util.h"
#include "cam_debug_util.h"

#if defined(CONFIG_SAMSUNG_OIS_MCU_STM32)
#include "cam_ois_mcu_stm32g.h"
#endif
#if defined(CONFIG_SAMSUNG_OIS_RUMBA_S4)
#include "cam_ois_rumba_s4.h"
#endif

int cam_ois_thread_add_msg(
	struct cam_ois_ctrl_t *o_ctrl,
	struct cam_ois_thread_msg_t *msg)
{
	unsigned long flags;

	if (!o_ctrl) {
		CAM_ERR(CAM_OIS, "Invalid Args");
		return -EINVAL;
	}

	if (!o_ctrl->is_thread_started) {
		CAM_ERR(CAM_OIS, "Thread is not started");
		return -EINVAL;
	}

	spin_lock_irqsave(&(o_ctrl->thread_spinlock), flags);
	list_add_tail(&(msg->list),
		&(o_ctrl->list_head_thread.list));
	spin_unlock_irqrestore(&(o_ctrl->thread_spinlock), flags);
	wake_up(&(o_ctrl->wait));

	return 0;
}

static struct cam_ois_thread_msg_t * __cam_ois_mcu_thread_get_msg(struct cam_ois_ctrl_t *o_ctrl)
{
	struct cam_ois_thread_msg_t *msg = NULL;
	unsigned long flags;

	spin_lock_irqsave(&(o_ctrl->thread_spinlock), flags);

	msg = list_first_entry_or_null(
			&o_ctrl->list_head_thread.list,
			struct cam_ois_thread_msg_t, list);

	if (msg != NULL)
		list_del(&(msg->list));

	spin_unlock_irqrestore(&(o_ctrl->thread_spinlock), flags);

	return msg;
}

#if defined(CONFIG_SAMSUNG_OIS_TAMODE_CONTROL)
static int __cam_ois_mcu_thread_set_tamode(struct cam_ois_ctrl_t *o_ctrl, struct cam_ois_thread_msg_t *msg)
{
	int rc = 0;

	CAM_DBG(CAM_OIS, "CAM_OIS_THREAD_MSG_SET_TAMODE");

	mutex_lock(&(o_ctrl->ois_mode_mutex));
	rc = cam_ois_set_ta_mode(o_ctrl);
	if (rc < 0)
		CAM_ERR(CAM_OIS, "set ta mode failed %d", rc);

	mutex_unlock(&(o_ctrl->ois_mode_mutex));

	return rc;
}
#endif

static int __cam_ois_mcu_thread_msg_reset(struct cam_ois_ctrl_t *o_ctrl, struct cam_ois_thread_msg_t *msg)
{
	int rc = 0;

	CAM_DBG(CAM_OIS, "CAM_OIS_THREAD_MSG_RESET_MCU");

	mutex_lock(&(o_ctrl->ois_mode_mutex));

	rc = cam_ois_set_ois_mode(o_ctrl, 0x16);
	if (rc < 0)
		CAM_ERR(CAM_OIS, "OIS centering failed %d", rc);

	mutex_unlock(&(o_ctrl->ois_mode_mutex));

	return rc;
}

static int __cam_ois_mcu_thread_msg_apply_setting(struct cam_ois_ctrl_t *o_ctrl, struct cam_ois_thread_msg_t *msg)
{
	int rc = 0;

	CAM_DBG(CAM_OIS, "CAM_OIS_THREAD_MSG_APPLY_SETTING");

	mutex_lock(&(o_ctrl->ois_mode_mutex));
	mutex_lock(&(o_ctrl->i2c_mode_data_mutex));

	rc = cam_ois_apply_settings(o_ctrl, msg->i2c_reg_settings);
	if (rc < 0) {
		if (msg->i2c_reg_settings->is_settings_valid != 1)
			CAM_WARN(CAM_OIS, "Cannot apply settings");
		else
			CAM_ERR(CAM_OIS, "Cannot apply settings");
	}

	if (msg->i2c_reg_settings->is_settings_valid == 1) {
		rc = delete_request(msg->i2c_reg_settings);
		if (rc < 0)
			CAM_ERR(CAM_OIS,
				"delete request: %lld rc: %d",
				msg->i2c_reg_settings->request_id, rc);
	}

	mutex_unlock(&(o_ctrl->i2c_mode_data_mutex));
	mutex_unlock(&(o_ctrl->ois_mode_mutex));

	return rc;
}

static int __cam_ois_mcu_thread_msg_start(struct cam_ois_ctrl_t *o_ctrl, struct cam_ois_thread_msg_t *msg)
{
	int rc = 0;

	CAM_DBG(CAM_OIS, "CAM_OIS_THREAD_MSG_START");

	mutex_lock(&(o_ctrl->ois_mode_mutex));
	usleep_range(10000, 10050);

	rc = cam_ois_init(o_ctrl);
	if (rc < 0)
		CAM_ERR(CAM_OIS, "OIS init failed %d", rc);

#if !defined(CONFIG_SAMSUNG_OIS_RUMBA_S4)
	// OIS centering
	cam_ois_set_ois_mode(o_ctrl, 0x05);
	if (rc < 0)
		CAM_ERR(CAM_OIS, "OIS centering failed %d", rc);
#endif

	msleep(40);
	mutex_unlock(&(o_ctrl->ois_mode_mutex));

	return rc;
}

static int __cam_ois_mcu_thread_handle_msg(struct cam_ois_ctrl_t *o_ctrl, struct cam_ois_thread_msg_t *msg)
{
	int rc = 0;

	if ((msg->msg_type < 0) ||
		(msg->msg_type >= CAM_OIS_THREAD_MSG_MAX)) {
		CAM_ERR(CAM_OIS, "Invalid OIS Message Type : %d", msg->msg_type);
		return rc;
	}

	switch (msg->msg_type) {
		case CAM_OIS_THREAD_MSG_START:
			rc = __cam_ois_mcu_thread_msg_start(o_ctrl, msg);
			break;
		case CAM_OIS_THREAD_MSG_APPLY_SETTING:
			rc = __cam_ois_mcu_thread_msg_apply_setting(o_ctrl, msg);
			break;
		case CAM_OIS_THREAD_MSG_RESET:
			rc = __cam_ois_mcu_thread_msg_reset(o_ctrl, msg);
			break;

#if defined(CONFIG_SAMSUNG_OIS_TAMODE_CONTROL)
		case CAM_OIS_THREAD_MSG_SET_TAMODE:
			rc = __cam_ois_mcu_thread_set_tamode(o_ctrl, msg);
			break;
#endif
	}

	return rc;
}

static int cam_ois_thread_func(void *data)
{
	int rc = 0;
	struct cam_ois_ctrl_t *o_ctrl = NULL;
	struct cam_ois_thread_msg_t *msg = NULL;

	if (!data) {
		CAM_ERR(CAM_OIS, "Invalid Args");
		return -EINVAL;
	}

	CAM_INFO(CAM_OIS, "E");

	o_ctrl = (struct cam_ois_ctrl_t *)data;
	o_ctrl->is_thread_started = true;

	while (true) {
		wait_event_freezable(
				o_ctrl->wait,
				(!list_empty(&(o_ctrl->list_head_thread.list)))
				|| kthread_should_stop());

		if (!o_ctrl->is_thread_started) {
			CAM_INFO(CAM_OIS, "Thread is stopped");
			break;
		}

		msg = __cam_ois_mcu_thread_get_msg(o_ctrl);

		if (msg != NULL) {
			rc = __cam_ois_mcu_thread_handle_msg(o_ctrl, msg);
			kfree(msg);
			msg = NULL;
		}
	}

	CAM_INFO(CAM_OIS, "X");

	return rc;
}

int cam_ois_thread_create(struct cam_ois_ctrl_t *o_ctrl)
{
	int rc = 0;
	int retries = 100;

	CAM_INFO(CAM_OIS, "E");

	if (!o_ctrl) {
		CAM_ERR(CAM_OIS, "Invalid Args");
		return -EINVAL;
	}

	if (o_ctrl->is_thread_started) {
		CAM_ERR(CAM_OIS, "Already started");
		return -EBUSY;
	}

	INIT_LIST_HEAD(&o_ctrl->list_head_thread.list);
	spin_lock_init(&(o_ctrl->thread_spinlock));
	o_ctrl->is_thread_started = false;
	o_ctrl->ois_thread = kthread_run(cam_ois_thread_func, (void *)o_ctrl, "CAM_OIS");
	if (IS_ERR(o_ctrl->ois_thread))
		return -EINVAL;

	while (o_ctrl->is_thread_started == false) {
		usleep_range(2000, 2100);
		if (retries < 0) {
			CAM_ERR(CAM_OIS, "Fail to start thread");
			rc = -EBUSY;
			break;
		}
		retries--;
	}

	CAM_INFO(CAM_OIS, "X");
	return rc;
}

int cam_ois_thread_destroy(struct cam_ois_ctrl_t *o_ctrl)
{
	struct cam_ois_thread_msg_t *msg_list = NULL, *msg_next = NULL;
	unsigned long flags;

	CAM_INFO(CAM_OIS, "E");

	if (!o_ctrl) {
		CAM_ERR(CAM_OIS, "Invalid Args");
		return -EINVAL;
	}

	if (!o_ctrl->is_thread_started) {
		CAM_WARN(CAM_OIS, "Thread is not started");
		return 0;
	}

	o_ctrl->is_thread_started = false;
	if (o_ctrl->ois_thread) {
		spin_lock_irqsave(&(o_ctrl->thread_spinlock), flags);
		list_for_each_entry_safe(msg_list, msg_next,
			&o_ctrl->list_head_thread.list, list) {
			list_del(&(msg_list->list));
			kfree(msg_list);
		}
		spin_unlock_irqrestore(&(o_ctrl->thread_spinlock), flags);

		kthread_stop(o_ctrl->ois_thread);
		wake_up(&o_ctrl->wait);
		o_ctrl->ois_thread = NULL;
	}

	CAM_INFO(CAM_OIS, "X");

	return 0;
}
