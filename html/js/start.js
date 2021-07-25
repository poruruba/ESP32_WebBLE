'use strict';

//const vConsole = new VConsole();
//window.datgui = new dat.GUI();

const log_separator = "\n----------\n";

var vue_options = {
    el: "#top",
    mixins: [mixins_bootstrap],
    data: {
        ws_socket: null,
        ws_url: "",
        ws_console_message: "",
        ws_send_message: "",
        ws_status_message: "closed",

        duration: 5,
        address: "",
        serviceuuid: "",
        uuid: "",
        withresponse: false,
        value: "",
        addrtype: "",
        scan_serviceuuid: "",
    },
    computed: {
    },
    methods: {
        ws_open: function () {
            this.ws_close();

            try {
                this.ws_socket = new WebSocket(this.ws_url);
                this.ws_socket.binaryType = "arraybuffer";
                this.ws_socket.onopen = (event) => {
                    this.console_log(this.make_console_state_message(event));
                    var date_string = new Date().toLocaleString('ja-JP', {});
                    this.ws_status_message = "opened from " + date_string;
                };
                this.ws_socket.onclose = (event) => {
                    this.console_log(this.make_console_close_message(event));
                    this.ws_socket = null;
                    this.ws_status_message = "closed";
                };
                this.ws_socket.onerror = (event) => {
                    this.console_log(this.make_console_state_message(event));
                };
                this.ws_socket.onmessage = (event) => {
                    this.console_log(this.make_console_input_message(event));
                };
            } catch (error) {
                alert(error);
            }
        },
        ws_close: function () {
            if (this.ws_socket)
                this.ws_socket.close();
        },
        ws_send: function () {
            if (this.ws_send_type == "text") {
                this.ws_socket.send(this.ws_send_message);
                this.console_log(this.make_console_output_message("text", this.ws_send_message));
            } else {
                var data = this.hex2ba(this.ws_send_message);
                var array = new Uint8Array(data);
                this.ws_socket.send(array);
                this.console_log(this.make_console_output_message("binary", array));
            }
        },
        console_clear: function () {
            this.ws_console_message = "";
        },
        make_console_output_message: function (type, data) {
            var date_string = new Date().toLocaleString('ja-JP', {});
            var message;
            if (typeof (data) == "string" || data instanceof String) {
                message = data;
            }else
            if (Array.isArray(data) ){
                message = this.ba2hex(data);
            }else{
                message = JSON.stringify(data, null, "\t");
            }
            return "[SEND] message " + type + " " + date_string + "\n" + message;
        },
        make_console_state_message: function (event) {
            var date_string = new Date().toLocaleString('ja-JP', {});
            return "[STATE] " + event.type + " " + date_string;
        },
        make_console_close_message: function (event) {
            var date_string = new Date().toLocaleString('ja-JP', {});
            return "[STATE] " + event.type + " code=" + event.code + " " + date_string;
        },
        make_console_input_message: function (event) {
            var date_string = new Date().toLocaleString('ja-JP', {});
            var message;
            var type;
            if (typeof (event.data) == "string" || event.data instanceof String) {
                try{
                    message = JSON.stringify(JSON.parse(event.data), null, "\t");
                    type = "json";
                }catch(error){
                    message = event.data;
                    type = "text";
                }
            } else {
                message = this.ba2hex(event.data);
                type = "binary";
            }

            return "[RECV] " + event.type + " " + type + " " + date_string + "\n" + message;
        },
        console_log: function (message) {
            this.ws_console_message = message + log_separator + this.ws_console_message;
        },
        ble_scan: function () {
            var message = {
                type: "scan",
                duration: this.duration
            };
            if (this.scan_serviceuuid )
                message.serviceuuid = this.scan_serviceuuid;
            this.ws_socket.send(JSON.stringify(message));
            this.console_log(this.make_console_output_message(message.type, message));
        },
        ble_scanresult: function(){
            var message = {
                type: "scanresult",
                address: this.address
            };
            this.ws_socket.send(JSON.stringify(message));
            this.console_log(this.make_console_output_message(message.type, message));
        },
        ble_connect: function () {
            var message = {
                type: "connect",
                address: this.address
            };
            if (this.addrtype)
                message.addresstype = parseInt(this.addrtype);
            this.ws_socket.send(JSON.stringify(message));
            this.console_log(this.make_console_output_message(message.type, message));
        },
        ble_disconnect: function () {
            var message = {
                type: "disconnect",
            };
            this.ws_socket.send(JSON.stringify(message));
            this.console_log(this.make_console_output_message(message.type, message));
        },
        ble_getallprimaryservices: function () {
            var message = {
                type: "primary",
            };
            this.ws_socket.send(JSON.stringify(message));
            this.console_log(this.make_console_output_message(message.type, message));
        },
        ble_getprimaryservice: function () {
            var message = {
                type: "service",
                serviceuuid: this.serviceuuid
            };
            this.ws_socket.send(JSON.stringify(message));
            this.console_log(this.make_console_output_message(message.type, message));
        },
        ble_getallcharacteristics: function () {
            var message = {
                type: "chars",
                serviceuuid: this.serviceuuid
            };
            this.ws_socket.send(JSON.stringify(message));
            this.console_log(this.make_console_output_message(message.type, message));
        },
        ble_getcharacteristic: function () {
            var message = {
                type: "char",
                serviceuuid: this.serviceuuid,
                uuid: this.uuid
            };
            this.ws_socket.send(JSON.stringify(message));
            this.console_log(this.make_console_output_message(message.type, message));
        },
        ble_startnotification: function () {
            var message = {
                type: "startnotify",
                uuid: this.uuid
            };
            this.ws_socket.send(JSON.stringify(message));
            this.console_log(this.make_console_output_message(message.type, message));
        },
        ble_stopnotification: function () {
            var message = {
                type: "stopnotify",
                uuid: this.uuid
            };
            this.ws_socket.send(JSON.stringify(message));
            this.console_log(this.make_console_output_message(message.type, message));
        },
        ble_readvalue: function () {
            var message = {
                type: "read",
                uuid: this.uuid
            };
            this.ws_socket.send(JSON.stringify(message));
            this.console_log(this.make_console_output_message(message.type, message));
        },
        ble_writevalue: function () {
            var message = {
                type: "write",
                uuid: this.uuid,
                withresponse: this.withresponse,
                value: this.value,
            };
            this.ws_socket.send(JSON.stringify(message));
            this.console_log(this.make_console_output_message(message.type, message));
        },
    },
    created: function(){
    },
    mounted: function(){
        proc_load();
    }
};
vue_add_data(vue_options, { progress_title: '' }); // for progress-dialog
vue_add_global_components(components_bootstrap);
vue_add_global_components(components_utils);

/* add additional components */
  
window.vue = new Vue( vue_options );
