#define PURPLE_PLUGINS

#include "conversation.h"
#include "debug.h"
#include "plugin.h"
#include "version.h"

//TODO make it work for !Pidgin
#include "gtkconv.h"
#include "gtkplugin.h"

#ifdef ENABLE_NLS
#	define GETTEXT_PACKAGE "nickchange"
#	include <glib/gi18n-lib.h>
#else
#	define _(String) ((const char *)String)
#	define N_(String) (String)
#endif

#define NICKCHANGE_PLUGIN_ID "core-eionrobb-nickchange"

//From libpurple core, decides whether to display nick changes or not
#define PURPLE_NICKCHANGE_PREF "/purple/conversations/chat/show_nick_change"

// The number of minutes before a person is considered
// to have stopped being part of active conversation.
#define DELAY_PREF "/plugins/core/nickchange/delay"
#define DELAY_DEFAULT 10

// The number of people that must be in a room for this
// plugin to have any effect
#define THRESHOLD_PREF "/plugins/core/nickchange/threshold"
#define THRESHOLD_DEFAULT 20

/* Hide buddies */
#define HIDE_BUDDIES_PREF "/plugins/core/nickchange/hide_buddies"
#define HIDE_BUDDIES_DEFAULT FALSE

struct joinpart_key
{
	PurpleConversation *conv;
	char *user;
};

static GHashTable *userstable = NULL;
static void (*orig_chat_rename_user)(PurpleConversation *conv, const char *old_name,
	                         const char *new_name, const char *new_alias) = NULL;
static guint clean_user_timeout;

static guint joinpart_key_hash(const struct joinpart_key *key)
{
	g_return_val_if_fail(key != NULL, 0);

	return g_direct_hash(key->conv) + g_str_hash(key->user);
}

static gboolean joinpart_key_equal(const struct joinpart_key *a, const struct joinpart_key *b)
{
	if (a == NULL)
		return (b == NULL);
	else if (b == NULL)
		return FALSE;

	return (a->conv == b->conv) && g_str_equal(a->user, b->user);
}

static void joinpart_key_destroy(struct joinpart_key *key)
{
	g_return_if_fail(key != NULL);

	g_free(key->user);
	g_free(key);
}

static gboolean should_hide_notice(PurpleConversation *conv, const char *name,
                                   GHashTable *users)
{
	PurpleConvChat *chat;
	int threshold;
	struct joinpart_key key;
	time_t *last_said;

	g_return_val_if_fail(conv != NULL, FALSE);
	g_return_val_if_fail(purple_conversation_get_type(conv) == PURPLE_CONV_TYPE_CHAT, FALSE);

	/* If the room is small, don't bother. */
	chat = PURPLE_CONV_CHAT(conv);
	threshold = purple_prefs_get_int(THRESHOLD_PREF);
	if (g_list_length(purple_conv_chat_get_users(chat)) < threshold)
		return FALSE;

	if (!purple_prefs_get_bool(HIDE_BUDDIES_PREF) &&
	    purple_find_buddy(purple_conversation_get_account(conv), name))
		return FALSE;

	/* Only show the notice if the user has spoken recently. */
	key.conv = conv;
	key.user = (gchar *)name;
	last_said = g_hash_table_lookup(users, &key);
	if (last_said != NULL)
	{
		int delay = purple_prefs_get_int(DELAY_PREF);
		if (delay > 0 && (*last_said + (delay * 60)) >= time(NULL))
			return FALSE;
	}

	return TRUE;
}

/*static gboolean chat_buddy_leaving_cb(PurpleConversation *conv, const char *name,
                               const char *reason, GHashTable *users)
{
	return should_hide_notice(conv, name, users);
}

static gboolean chat_buddy_joining_cb(PurpleConversation *conv, const char *name,
                                      PurpleConvChatBuddyFlags flags,
                                      GHashTable *users)
{
	return should_hide_notice(conv, name, users);
}*/

static void received_chat_msg_cb(PurpleAccount *account, char *sender,
                                 char *message, PurpleConversation *conv,
                                 PurpleMessageFlags flags, GHashTable *users)
{
	struct joinpart_key key;
	time_t *last_said;

	/* Most of the time, we'll already have tracked the user,
	 * so we avoid memory allocation here. */
	key.conv = conv;
	key.user = sender;
	last_said = g_hash_table_lookup(users, &key);
	if (last_said != NULL)
	{
		/* They just said something, so update the time. */
		time(last_said);
	}
	else
	{
		struct joinpart_key *key2;

		key2 = g_new(struct joinpart_key, 1);
		key2->conv = conv;
		key2->user = g_strdup(sender);

		last_said = g_new(time_t, 1);
		time(last_said);

		g_hash_table_insert(users, key2, last_said);
	}
}

static gboolean check_expire_time(struct joinpart_key *key,
                                  time_t *last_said, time_t *limit)
{
	purple_debug_info("nickchange", "Removing key for %s\n", key->user);
	return (*last_said < *limit);
}

static gboolean clean_users_hash(GHashTable *users)
{
	int delay = purple_prefs_get_int(DELAY_PREF);
	time_t limit = time(NULL) - (60 * delay);

	g_hash_table_foreach_remove(users, (GHRFunc)check_expire_time, &limit);

	return TRUE;
}

/*static void
toggle_nickchange_pref(const char *name, PurplePrefType type, gconstpointer val, gpointer data)
{
	
}*/
static void nickchange_chat_rename_user
(PurpleConversation *conv, const char *old_user, const char *new_user, const char *new_alias)
{
	if (!should_hide_notice(conv, old_user, userstable)) {
		PurpleConvChat *chat = PURPLE_CONV_CHAT(conv);
		char tmp[2048];
		
		if (purple_strequal(chat->nick, purple_normalize(conv->account, old_user))) {
			// Its me!
			char *escaped = g_markup_escape_text(new_user, -1);
			g_snprintf(tmp, sizeof(tmp),
					_("You are now known as %s"), escaped);
			g_free(escaped);
		} else {
			const char *old_alias = old_user;
			const char *new_alias = new_user;
			char *escaped;
			char *escaped2;
			PurpleConnection *gc = purple_conversation_get_gc(conv);
			PurplePluginProtocolInfo *prpl_info;
			
			prpl_info = PURPLE_PLUGIN_PROTOCOL_INFO(purple_connection_get_prpl(gc));
			if (prpl_info && !(prpl_info->options & OPT_PROTO_UNIQUE_CHATNAME)) {
				PurpleBuddy *buddy;

				if ((buddy = purple_find_buddy(gc->account, old_user)) != NULL)
					old_alias = purple_buddy_get_contact_alias(buddy);
				if ((buddy = purple_find_buddy(gc->account, new_user)) != NULL)
					new_alias = purple_buddy_get_contact_alias(buddy);
			}

			escaped = g_markup_escape_text(old_alias, -1);
			escaped2 = g_markup_escape_text(new_alias, -1);
			g_snprintf(tmp, sizeof(tmp),
					_("%s is now known as %s"), escaped, escaped2);
			g_free(escaped);
			g_free(escaped2);
		}

		purple_conversation_write(conv, NULL, tmp,
				PURPLE_MESSAGE_SYSTEM | PURPLE_MESSAGE_NO_LINKIFY,
				time(NULL));
	}
	
	if (orig_chat_rename_user)
		return orig_chat_rename_user(conv, old_user, new_user, new_alias);
}

static gboolean plugin_load(PurplePlugin *plugin)
{
	void *conv_handle;
	PurpleConversationUiOps *ui_ops;
	
	purple_prefs_set_bool(PURPLE_NICKCHANGE_PREF, FALSE);

	userstable = g_hash_table_new_full((GHashFunc)joinpart_key_hash,
	                              (GEqualFunc)joinpart_key_equal,
	                              (GDestroyNotify)joinpart_key_destroy,
	                              g_free);

	conv_handle = purple_conversations_get_handle();
	//purple_signal_connect(conv_handle, "chat-buddy-joining", plugin, PURPLE_CALLBACK(chat_buddy_joining_cb), userstable);
	//purple_signal_connect(conv_handle, "chat-buddy-leaving", plugin, PURPLE_CALLBACK(chat_buddy_leaving_cb), userstable);
	purple_signal_connect(conv_handle, "received-chat-msg", plugin, PURPLE_CALLBACK(received_chat_msg_cb), userstable);

	//purple_prefs_connect_callback(plugin, const char *name, PurplePrefCallback cb, gpointer data)
	
	//Attempt overriding the global UI ops
	ui_ops = pidgin_conversations_get_conv_ui_ops();
	
	/* Cleanup every 5 minutes */
	clean_user_timeout = purple_timeout_add_seconds(60 * 5, (GSourceFunc)clean_users_hash, userstable);
	
	if (ui_ops) {
		orig_chat_rename_user = ui_ops->chat_rename_user;
		ui_ops->chat_rename_user = nickchange_chat_rename_user;
	} else {
		//TODO make it work for !Pidgin too
		return FALSE;
	}

	return TRUE;
}

static gboolean plugin_unload(PurplePlugin *plugin)
{
	PurpleConversationUiOps *ui_ops;
	
	purple_prefs_set_bool(PURPLE_NICKCHANGE_PREF, TRUE);

	/* Destroy the hash table. The core plugin code will
	 * disconnect the signals, and since Purple is single-threaded,
	 * we don't have to worry one will be called after this. */
	g_hash_table_destroy(userstable);

	purple_timeout_remove(clean_user_timeout);
	
	if (orig_chat_rename_user) {
		ui_ops = pidgin_conversations_get_conv_ui_ops();
		ui_ops->chat_rename_user = orig_chat_rename_user;
	}
	
	return TRUE;
}

static PurplePluginPrefFrame *
get_plugin_pref_frame(PurplePlugin *plugin)
{
	PurplePluginPrefFrame *frame;
	PurplePluginPref *ppref;

	g_return_val_if_fail(plugin != NULL, FALSE);

	frame = purple_plugin_pref_frame_new();

	ppref = purple_plugin_pref_new_with_label(_("Hide Nick Changes"));
	purple_plugin_pref_frame_add(frame, ppref);

	ppref = purple_plugin_pref_new_with_name_and_label(THRESHOLD_PREF,
	                                                 /* Translators: Followed by an input request a number of people */
	                                                 _("For rooms with more than this many people"));
	purple_plugin_pref_set_bounds(ppref, 0, 1000);
	purple_plugin_pref_frame_add(frame, ppref);

	ppref = purple_plugin_pref_new_with_name_and_label(DELAY_PREF,
	                                                 _("If user has not spoken in this many minutes"));
	purple_plugin_pref_set_bounds(ppref, 0, 8 * 60); /* 8 Hours */
	purple_plugin_pref_frame_add(frame, ppref);

	ppref = purple_plugin_pref_new_with_name_and_label(HIDE_BUDDIES_PREF,
	                                                 _("Apply hiding rules to buddies"));
	purple_plugin_pref_frame_add(frame, ppref);

	return frame;
}

static PurplePluginUiInfo prefs_info = {
	get_plugin_pref_frame,
	0,   /* page_num (reserved) */
	NULL, /* frame (reserved) */

	/* padding */
	NULL,
	NULL,
	NULL,
	NULL
};

static PurplePluginInfo info =
{
	PURPLE_PLUGIN_MAGIC,
	2,
	2,
	PURPLE_PLUGIN_STANDARD,                             /**< type           */
	//TODO
	PIDGIN_PLUGIN_TYPE,                                             /**< ui_requirement */
	0,                                                /**< flags          */
	NULL,                                             /**< dependencies   */
	PURPLE_PRIORITY_DEFAULT,                            /**< priority       */

	NICKCHANGE_PLUGIN_ID,                               /**< id             */
	N_("Nick Change Hiding"),                           /**< name           */
	"0.1",                                  /**< version        */
	                                                  /**  summary        */
	N_("Hides extraneous name changing messages."),
	                                                  /**  description    */
	N_("This plugin hides name change messages in large "
	   "rooms, except for those users actively taking "
	   "part in a conversation."),
	"Eion Robb <eionrobb@gmail.com>",             /**< author         */
	"",                                     /**< homepage       */

	plugin_load,                                      /**< load           */
	plugin_unload,                                    /**< unload         */
	NULL,                                             /**< destroy        */

	NULL,                                             /**< ui_info        */
	NULL,                                             /**< extra_info     */
	&prefs_info,                                      /**< prefs_info     */
	NULL,                                             /**< actions        */

	/* padding */
	NULL,
	NULL,
	NULL,
	NULL
};

static void
init_plugin(PurplePlugin *plugin)
{
	purple_prefs_add_none("/plugins/core/nickchange");

	purple_prefs_add_int(DELAY_PREF, DELAY_DEFAULT);
	purple_prefs_add_int(THRESHOLD_PREF, THRESHOLD_DEFAULT);
	purple_prefs_add_bool(HIDE_BUDDIES_PREF, HIDE_BUDDIES_DEFAULT);
	
#if ENABLE_NLS
	bindtextdomain(GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
#endif
}

PURPLE_INIT_PLUGIN(nickchange, init_plugin, info)
