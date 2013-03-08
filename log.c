/*
 * Copyright (C) 2013 Felix Fietkau <nbd@openwrt.org>
 * Copyright (C) 2013 John Crispin <blogic@openwrt.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * as published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <libubox/uloop.h>
#include <libubox/blobmsg_json.h>

#include "procd.h"
#include "syslog.h"

static struct ubus_subscriber log_event;

static int notify;
struct ubus_context *_ctx;
static struct blob_buf b;

static const struct blobmsg_policy read_policy =
	{ .name = "lines", .type = BLOBMSG_TYPE_INT32 };

static const struct blobmsg_policy write_policy =
	{ .name = "event", .type = BLOBMSG_TYPE_STRING };

static int read_log(struct ubus_context *ctx, struct ubus_object *obj,
		struct ubus_request_data *req, const char *method,
		struct blob_attr *msg)
{
	struct blob_attr *tb;
	struct log_head *l;
	void *lines, *entry;
	int count = 0;

	if (msg) {
		blobmsg_parse(&read_policy, 1, &tb, blob_data(msg), blob_len(msg));
		if (tb)
			count = blobmsg_get_u32(tb);
	}

	blob_buf_init(&b, 0);
	lines = blobmsg_open_array(&b, "lines");

	l = log_list(count, NULL);

	while (l) {
		entry = blobmsg_open_table(&b, NULL);
		blobmsg_add_string(&b, "msg", l->data);
		blobmsg_add_u32(&b, "id", l->id);
		blobmsg_add_u32(&b, "priority", l->priority);
		blobmsg_add_u32(&b, "source", l->source);
		blobmsg_add_u64(&b, "time", (l->ts.tv_sec * 1000) + (l->ts.tv_nsec / 1000000));
		blobmsg_close_table(&b, entry);
		l = log_list(count, l);
	}
	blobmsg_close_table(&b, lines);
	ubus_send_reply(ctx, req, b.head);

	return 0;
}

static int write_log(struct ubus_context *ctx, struct ubus_object *obj,
		struct ubus_request_data *req, const char *method,
		struct blob_attr *msg)
{
	struct blob_attr *tb;
	char *event;

	if (msg) {
		blobmsg_parse(&write_policy, 1, &tb, blob_data(msg), blob_len(msg));
		if (tb) {
			event = blobmsg_get_string(tb);
			log_add(event, strlen(event) + 1, SOURCE_SYSLOG);
		}
	}

	return 0;
}

static void log_subscribe_cb(struct ubus_context *ctx, struct ubus_object *obj)
{
	notify = obj->has_subscribers;
}

static const struct ubus_method log_methods[] = {
	{ .name = "read", .handler = read_log, .policy = &read_policy, .n_policy = 1 },
	{ .name = "write", .handler = write_log, .policy = &write_policy, .n_policy = 1 },
};

static struct ubus_object_type log_object_type =
	UBUS_OBJECT_TYPE("log", log_methods);

static struct ubus_object log_object = {
	.name = "log",
	.type = &log_object_type,
	.methods = log_methods,
	.n_methods = ARRAY_SIZE(log_methods),
	.subscribe_cb = log_subscribe_cb,
};

void ubus_notify_log(struct log_head *l)
{
	int ret;

	if (!notify)
		return;

	blob_buf_init(&b, 0);
	blobmsg_add_u32(&b, "id", l->id);
	blobmsg_add_u32(&b, "priority", l->priority);
	blobmsg_add_u32(&b, "source", l->source);
	blobmsg_add_u64(&b, "time", (l->ts.tv_sec * 1000) + (l->ts.tv_nsec / 1000000));

	ret = ubus_notify(_ctx, &log_object, l->data, b.head, -1);
	if (ret)
		ERROR("Failed to notify log: %s\n", ubus_strerror(ret));
}

void ubus_init_log(struct ubus_context *ctx)
{
	int ret;

	_ctx = ctx;

	ret = ubus_add_object(ctx, &log_object);
	if (ret)
		ERROR("Failed to add object: %s\n", ubus_strerror(ret));

	ret = ubus_register_subscriber(ctx, &log_event);
	if (ret)
		ERROR("Failed to add watch handler: %s\n", ubus_strerror(ret));
}