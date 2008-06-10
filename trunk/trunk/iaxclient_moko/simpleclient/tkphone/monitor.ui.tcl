# interface generated by SpecTcl version 1.1 from H:/asterisk/iaxclient/simpleclient/tkphone/monitor.ui
#   root     is the parent window for this user interface

proc monitor_ui {root args} {

	# this treats "." as a special case

	if {$root == "."} {
	    set base ""
	} else {
	    set base $root
	}
    
	frame $base.frame#3 \
		-borderwidth 2 \
		-relief ridge

	frame $base.frame#4 \
		-borderwidth 2 \
		-relief ridge

	label $base.label#3 \
		-text {audio control}

	label $base.label#4 \
		-text record

	label $base.label#5 \
		-text play

	scale $base.record \
		-from 100.0 \
		-orient v \
		-to 0.0 \
		-variable record

	scale $base.play \
		-from 100.0 \
		-orient v \
		-to 0.0 \
		-variable play

	button $base.dismiss_audio \
		-text dismiss


	# Geometry management

	grid $base.frame#3 -in $root	-row 2 -column 1  \
		-ipadx 2 \
		-ipady 2 \
		-padx 2 \
		-pady 2 \
		-sticky ns
	grid $base.frame#4 -in $root	-row 2 -column 2  \
		-ipadx 2 \
		-ipady 2 \
		-padx 2 \
		-pady 2 \
		-sticky ns
	grid $base.label#3 -in $root	-row 1 -column 1  \
		-columnspan 2
	grid $base.label#4 -in $base.frame#3	-row 1 -column 1 
	grid $base.label#5 -in $base.frame#4	-row 1 -column 1 
	grid $base.record -in $base.frame#3	-row 2 -column 1  \
		-sticky ns
	grid $base.play -in $base.frame#4	-row 2 -column 1  \
		-sticky ns
	grid $base.dismiss_audio -in $root	-row 3 -column 1  \
		-columnspan 2

	# Resize behavior management

	grid rowconfigure $base.frame#3 1 -weight 0 -minsize 30
	grid rowconfigure $base.frame#3 2 -weight 1 -minsize 30
	grid columnconfigure $base.frame#3 1 -weight 0 -minsize 30

	grid rowconfigure $root 1 -weight 0 -minsize 30
	grid rowconfigure $root 2 -weight 1 -minsize 30
	grid rowconfigure $root 3 -weight 0 -minsize 30
	grid columnconfigure $root 1 -weight 0 -minsize 11
	grid columnconfigure $root 2 -weight 0 -minsize 25

	grid rowconfigure $base.frame#4 1 -weight 0 -minsize 30
	grid rowconfigure $base.frame#4 2 -weight 1 -minsize 30
	grid columnconfigure $base.frame#4 1 -weight 0 -minsize 30
# additional interface code
# end additional interface code

}


# Allow interface to be run "stand-alone" for testing

catch {
    if [info exists embed_args] {
	# we are running in the plugin
	monitor_ui .
    } else {
	# we are running in stand-alone mode
	if {$argv0 == [info script]} {
	    wm title . "Testing monitor_ui"
	    monitor_ui .
	}
    }
}
