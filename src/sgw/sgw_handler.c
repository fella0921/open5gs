#define TRACE_MODULE _sgw_handler

#include "core_debug.h"

#include "gtp_types.h"

#include "sgw_event.h"
#include "sgw_context.h"
#include "sgw_path.h"
#include "sgw_handler.h"

void sgw_handle_create_session_request(
        gtp_xact_t *xact, c_uint8_t type, gtp_message_t *gtp_message)
{
    status_t rv;
    gtp_create_session_request_t *req = NULL;
    pkbuf_t *pkbuf = NULL;
    gtp_f_teid_t *mme_s11_teid = NULL;
    gtp_f_teid_t sgw_s5c_teid, sgw_s5u_teid;

    sgw_sess_t *sess = NULL;
    sgw_bearer_t *bearer = NULL;

    d_assert(xact, return, "Null param");
    d_assert(gtp_message, return, "Null param");

    req = &gtp_message->create_session_request;

    if (req->sender_f_teid_for_control_plane.presence == 0)
    {
        d_error("No GTP TEID");
        return;
    }
    if (req->bearer_contexts_to_be_created.presence == 0)
    {
        d_error("No Bearer");
        return;
    }
    if (req->bearer_contexts_to_be_created.eps_bearer_id.presence == 0)
    {
        d_error("No EPS Bearer ID");
        return;
    }

    /* Generate Control Plane(UL) : SGW-S11 */
    /* Generate Control Plane(DL) : SGW-S5C */
    /* Generate Data Plane(UL) : SGW-S1U */
    /* Generate Data Plane(DL) : SGW-S5U */
    bearer = sgw_sess_add(
            req->bearer_contexts_to_be_created.eps_bearer_id.u8);
    d_assert(bearer, return, "No Bearer Context");
    sess = bearer->sess;
    d_assert(sess, return, "Null param");

    /* Receive Control Plane(DL) : MME-S11 */
    mme_s11_teid = req->sender_f_teid_for_control_plane.data;
    sess->mme_s11_teid = ntohl(mme_s11_teid->teid);
    sess->mme_s11_addr = mme_s11_teid->ipv4_addr;

    /* Send Control Plane(DL) : SGW-S5C */
    memset(&sgw_s5c_teid, 0, sizeof(gtp_f_teid_t));
    sgw_s5c_teid.teid = htonl(sess->sgw_s5c_teid);
    sgw_s5c_teid.ipv4_addr = sess->sgw_s5c_addr;
    sgw_s5c_teid.interface_type = GTP_F_TEID_S5_S8_SGW_GTP_C;
    req->sender_f_teid_for_control_plane.presence = 1;
    req->sender_f_teid_for_control_plane.data = &sgw_s5c_teid;
    req->sender_f_teid_for_control_plane.len = GTP_F_TEID_IPV4_LEN;

    /* Remove PGW-S5C */
    req->pgw_s5_s8_address_for_control_plane_or_pmip.presence = 0;

    /* Send Data Plane(DL) : SGW-S5U */
    memset(&sgw_s5u_teid, 0, sizeof(gtp_f_teid_t));
    sgw_s5u_teid.teid = htonl(bearer->sgw_s5u_teid);
    sgw_s5u_teid.ipv4_addr = bearer->sgw_s5u_addr;
    sgw_s5u_teid.interface_type = GTP_F_TEID_S5_S8_SGW_GTP_U;
    req->bearer_contexts_to_be_created.s5_s8_u_sgw_f_teid.presence = 1;
    req->bearer_contexts_to_be_created.s5_s8_u_sgw_f_teid.data = &sgw_s5u_teid;
    req->bearer_contexts_to_be_created.s5_s8_u_sgw_f_teid.len = 
        GTP_F_TEID_IPV4_LEN;

    rv = gtp_build_msg(&pkbuf, type, gtp_message);
    d_assert(rv == CORE_OK, return, "gtp build failed");

    d_assert(sgw_s5c_send_to_pgw(xact, type, 0, pkbuf) == CORE_OK, 
            return, "failed to send message");

    d_info("[GTP] Create Session Reqeust : "
            "MME[%d] --> SGW", sess->mme_s11_teid);
}

void sgw_handle_create_session_response(gtp_xact_t *xact, 
    sgw_sess_t *sess, c_uint8_t type, gtp_message_t *gtp_message)
{
    status_t rv;
    sgw_bearer_t *bearer = NULL;
    gtp_create_session_response_t *rsp = NULL;
    pkbuf_t *pkbuf = NULL;

    gtp_f_teid_t *pgw_s5c_teid = NULL;
    gtp_f_teid_t sgw_s11_teid;
    gtp_f_teid_t *pgw_s5u_teid = NULL;
    gtp_f_teid_t sgw_s1u_teid;

    d_assert(sess, return, "Null param");
    d_assert(xact, return, "Null param");
    d_assert(gtp_message, return, "Null param");

    rsp = &gtp_message->create_session_response;

    if (rsp->pgw_s5_s8__s2a_s2b_f_teid_for_pmip_based_interface_or_for_gtp_based_control_plane_interface.
            presence == 0)
    {
        d_error("No GTP TEID");
        return;
    }
    if (rsp->bearer_contexts_created.presence == 0)
    {
        d_error("No Bearer");
        return;
    }
    if (rsp->bearer_contexts_created.eps_bearer_id.presence == 0)
    {
        d_error("No EPS Bearer ID");
        return;
    }
    if (rsp->bearer_contexts_created.s5_s8_u_sgw_f_teid.presence == 0)
    {
        d_error("No GTP TEID");
        return;
    }

    bearer = sgw_bearer_find_by_id(sess, 
                rsp->bearer_contexts_created.eps_bearer_id.u8);
    d_assert(bearer, sgw_sess_remove(sess); return, "No Bearer Context");

    /* Receive Control Plane(UL) : PGW-S5C */
    pgw_s5c_teid = rsp->pgw_s5_s8__s2a_s2b_f_teid_for_pmip_based_interface_or_for_gtp_based_control_plane_interface.
                data;
    sess->pgw_s5c_teid = ntohl(pgw_s5c_teid->teid);
    sess->pgw_s5c_addr = pgw_s5c_teid->ipv4_addr;
    rsp->pgw_s5_s8__s2a_s2b_f_teid_for_pmip_based_interface_or_for_gtp_based_control_plane_interface.
                presence = 0;

    /* Receive Data Plane(UL) : PGW-S5U */
    pgw_s5u_teid = rsp->bearer_contexts_created.s5_s8_u_sgw_f_teid.data;
    bearer->pgw_s5u_teid = ntohl(pgw_s5u_teid->teid);
    bearer->pgw_s5u_addr = pgw_s5u_teid->ipv4_addr;
    rsp->bearer_contexts_created.s5_s8_u_sgw_f_teid.presence = 0;

    /* Send Control Plane(UL) : SGW-S11 */
    memset(&sgw_s11_teid, 0, sizeof(gtp_f_teid_t));
    sgw_s11_teid.ipv4 = 1;
    sgw_s11_teid.interface_type = GTP_F_TEID_S11_S4_SGW_GTP_C;
    sgw_s11_teid.ipv4_addr = sess->sgw_s11_addr;
    sgw_s11_teid.teid = htonl(sess->sgw_s11_teid);
    rsp->sender_f_teid_for_control_plane.presence = 1;
    rsp->sender_f_teid_for_control_plane.data = &sgw_s11_teid;
    rsp->sender_f_teid_for_control_plane.len = GTP_F_TEID_IPV4_LEN;

    /* Send Data Plane(UL) : SGW-S1U */
    memset(&sgw_s1u_teid, 0, sizeof(gtp_f_teid_t));
    sgw_s1u_teid.ipv4 = 1;
    sgw_s1u_teid.interface_type = GTP_F_TEID_S1_U_SGW_GTP_U;
    sgw_s1u_teid.ipv4_addr = bearer->sgw_s1u_addr;
    sgw_s1u_teid.teid = htonl(bearer->sgw_s1u_teid);
    rsp->bearer_contexts_created.s1_u_enodeb_f_teid.presence = 1;
    rsp->bearer_contexts_created.s1_u_enodeb_f_teid.data = &sgw_s1u_teid;
    rsp->bearer_contexts_created.s1_u_enodeb_f_teid.len = GTP_F_TEID_IPV4_LEN;

    rv = gtp_build_msg(&pkbuf, type, gtp_message);
    d_assert(rv == CORE_OK, return, "gtp build failed");

    d_assert(sgw_s11_send_to_mme(
            xact, type, sess->mme_s11_teid, pkbuf) == CORE_OK, return, 
            "failed to send message");
    d_info("[GTP] Create Session Response : "
            "SGW[%d] <-- PGW[%d]", sess->sgw_s5c_teid, sess->pgw_s5c_teid);
}

CORE_DECLARE(void) sgw_handle_modify_bearer_request(gtp_xact_t *xact,
    sgw_sess_t *sess, gtp_modify_bearer_request_t *req)
{
    status_t rv;
    sgw_bearer_t *bearer = NULL;
    gtp_modify_bearer_response_t *rsp = NULL;
    pkbuf_t *pkbuf = NULL;
    gtp_message_t gtp_message;
    
    gtp_cause_t cause;
    gtp_f_teid_t *enb_s1u_teid = NULL;

    d_assert(sess, return, "Null param");
    d_assert(xact, return, "Null param");

    if (req->bearer_contexts_to_be_modified.presence == 0)
    {
        d_error("No Bearer");
        return;
    }
    if (req->bearer_contexts_to_be_modified.eps_bearer_id.presence == 0)
    {
        d_error("No EPS Bearer ID");
        return;
    }
    if (req->bearer_contexts_to_be_modified.s1_u_enodeb_f_teid.presence == 0)
    {
        d_error("No GTP TEID");
        return;
    }

    bearer = sgw_bearer_find_by_id(sess, 
                req->bearer_contexts_to_be_modified.eps_bearer_id.u8);
    d_assert(bearer, sgw_sess_remove(sess); return, "No Bearer Context");

    /* Receive Data Plane(DL) : eNB-S1U */
    enb_s1u_teid = req->bearer_contexts_to_be_modified.s1_u_enodeb_f_teid.data;
    bearer->enb_s1u_teid = ntohl(enb_s1u_teid->teid);
    bearer->enb_s1u_addr = enb_s1u_teid->ipv4_addr;

    rsp = &gtp_message.modify_bearer_response;
    memset(&gtp_message, 0, sizeof(gtp_message_t));

    memset(&cause, 0, sizeof(cause));
    cause.value = GTP_CAUSE_REQUEST_ACCEPTED;

    rsp->cause.presence = 1;
    rsp->cause.data = &cause;
    rsp->cause.len = sizeof(cause);

    rv = gtp_build_msg(&pkbuf, GTP_MODIFY_BEARER_RESPONSE_TYPE, &gtp_message);
    d_assert(rv == CORE_OK, return, "gtp build failed");

    d_assert(sgw_s11_send_to_mme(xact, GTP_MODIFY_BEARER_RESPONSE_TYPE, 
            sess->mme_s11_teid, pkbuf) == CORE_OK, return, 
            "failed to send message");

    d_info("[GTP] Modify Bearer Reqeust : "
            "MME[%d] --> SGW[%d]", sess->mme_s11_teid, sess->sgw_s11_teid);
}
