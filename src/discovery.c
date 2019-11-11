#include "discovery.h"

/*
 * Starts command processing loop for the admin queue of the discovery
 * controller. Returns if the connection is broken.
 */
void start_discovery_queue(sock_t socket, struct nvme_cmd* conn_cmd) {
	log_info("Starting new discovery admin queue");
	u16 qsize = conn_cmd->cdw11 & 0xffff;
	u16 sqhd = 2;
	struct nvme_properties props = {
		.cap  = ((u64)1<<37) | (4<<24) | (1<<16) | 63,
		.vs   = 0x10400,
		.cc   = 0,
		.csts = 0,
	};
	struct nvme_status status = {
		.dw0  = 1,
		.dw1  = 0,
		.sqhd = 1,
		.sqid = 0,
		.cid  = conn_cmd->cid,
		.sf   = 0,
	};
	int err = send_status(socket, &status);
	if (err) {
		log_warn("Failed to send response");
		return;
	}

	// processing loop
	struct nvme_cmd* cmd;
	while (1) {
		memset(&status, 0, NVME_STATUS_LEN);
		status.sqhd = sqhd++;
		if (sqhd >= qsize) sqhd = 0;
		cmd = recv_cmd(socket, NULL);
		if (!cmd) {
			log_warn("Failed to receive command");
			return;
		}
		status.cid = cmd->cid;
		log_debug("Got command: 0x%x", cmd->opcode);

		if (cmd->opcode == OPC_FABRICS)
			fabric_cmd(&props, cmd, &status);
		else if (props.cc & 0x1) {
			switch (cmd->opcode) {
				case OPC_IDENTIFY:
					discovery_identify(socket, cmd, &status);
					break;
				default:
					status.sf = make_sf(SCT_GENERIC, SC_INVALID_OPCODE);
			}
		}
		else
			status.sf = make_sf(SCT_GENERIC, SC_COMMAND_SEQ);

		free(cmd);
		err = send_status(socket, &status);
		if (err) {
			log_warn("Failed to send response");
			return;
		}
	}
}

/*
 * Processes an identify command.
 */
void discovery_identify(sock_t socket, struct nvme_cmd* cmd, struct nvme_status* status) {
	log_debug("Identify: CNS=0x%x, NSID=0x%x", cmd->cdw10, cmd->nsid);
	if (cmd->cdw10 != CNS_ID_CTRL) {
		status->sf = make_sf(SCT_GENERIC, SC_INVALID_FIELD);
		return;
	}
	struct nvme_identify_ctrl id_ctrl = {0};
	strcpy(id_ctrl.fr, "0.0.1");
	strcpy(id_ctrl.subnqn, DISCOVERY_NQN);
	id_ctrl.mdts = 1;
	id_ctrl.cntlid = 1;
	id_ctrl.ver = 0x10400;

	send_data(socket, cmd->cid, &id_ctrl, NVME_ID_CTRL_LEN);
}

/*
 * Processes a get log page command.
 */
void discovery_get_log(sock_t socket, struct nvme_cmd* cmd, struct nvme_status* status) {
	u8  lid = cmd->cdw10 & 0xff;
	u32 bytes = ((cmd->cdw10 >> 16) & 0x0fff) * 4;
	log_debug("Get log page: LID=0x%x, %d bytes", lid, bytes);
}

