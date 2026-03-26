#ifndef COM_H__
#define COM_H__

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Callback when device successfully joins main network
 *
 * @details
 * Called by provisioning complete handler to trigger post-join actions
 * (e.g. delayed brightness update).
 * Should be called only on fresh provisioning events.
 */
void app_on_network_joined(void);

#ifdef __cplusplus
}
#endif

#endif /* COM_H__ */
